# Comptime

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page is how tlc folds a `comptime` value and specializes a function while compiling.

## A comptime function

A `comptime func` calculates while the compiler is still compiling the program.

Every argument has to be a comptime value, even when the parameter is not marked `comptime`.

A `return` leaves the `comptime func` and finishes the calculation for that call.

The call is an expression in the caller.

The compiler does not paste the function body into the caller at that expression.

## Comptime values

A comptime value is a folded integer, a constant `[]const u8`, or an enumerator.

A comptime value is also a `comptime const` of a struct, a float, an alias, or a function.

A string is a literal, a constant range, or a `comptime const` in the same file.

A `var` is not a comptime value.

A runtime `const` is not a comptime value, until `comptime const folded = name`.

A `comptime const` whose initializer folds to `i32` is a comptime argument, at file scope and inside a function.

That `comptime const` hides a `comptime const` of the same name.

A runtime `const` of a struct, a float, an alias, a function, a tuple, or `[]const u8` is not a comptime value.

A tuple of a folded integer, a `bool`, a constant `[]const u8`, an enumerator, or a file-scope `const` is a comptime value.

A tuple element that is a local `const` is `type mismatch`.

An element may be a `bool`, an `f64`, an `f32`, a struct, or a `[]const u8`.

An element may also be an enumerator, a tuple, a function, a pointer, or an array.

`comptime const pair = (40, 2)` names that tuple.

`comptime const nested = ((40, 2), 1)` names that tuple, and `nested[0][1]` is `2`.

`comptime const ops = (add, 1)` names a tuple whose element is that function, and `(ops[0])(40, 2)` calls `add`.

`comptime const pair = (&addr, 2)` names a tuple whose element is that pointer, and `*pair[0]` is `addr`.

`comptime const pair = (row, 0)` names that tuple when `row` is `[_]i32{40, 2}`.

The element is that array, and `pair[0][0]` is `40`.

A runtime `const` of a tuple is `type mismatch`.

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

`comptime const n = 41` then `return add(n)` folds the same way.

A runtime `const` and a `var` are `not supported yet`.

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

## When the call folds

The call folds when the body is one `return` of a constant `i32`, with no runtime symbol and no trace frame.

`emit` is not valid in a `comptime func`.

A `var` passed to a `comptime func` is `not supported yet`.

A type parameter, a method, `extern`, and `export` are `not supported yet`.

A call that does not fold is `not supported yet`.

## Conditions and switches

`comptime { ... }` inside an ordinary function is `not supported yet`.

`comptime for` is `not supported yet`.

`const for` is not a form.

`comptime if` keeps one arm when the condition folds.

A condition that is not comptime is `not supported yet`.

A `comptime` parameter is comptime.

The copy that still receives it typechecks both arms.

The copy that substitutes the value keeps one arm.

An ordinary `if` typechecks both arms.

`comptime switch` keeps the matching case and the statements that follow until `break` when the value folds.

A value that is not comptime is `not supported yet`.

A `comptime` parameter leaves every case until a call substitutes it.

An ordinary `switch` typechecks every case.

A `var` and a runtime `const` do not fold.

In a [macro](macro.md), `comptime if` and `comptime switch` are `not supported yet`.

In a macro, `for` and `if` run while compiling and do not take a `comptime` prefix.

A `switch` in a macro is `not supported yet`.

## A comptime parameter on an ordinary function

An ordinary function may mark a parameter `comptime`.

That argument has to be a comptime value, or the call is `type mismatch`.

When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds.

The compiler still compiles that function.

A mix of a `comptime` parameter and a runtime parameter is specialized per constant.

The compiler does not pass the comptime parameter.

Each use of that name in the copy is the constant.

`return scale(2, x)` calls that copy and adds `2`.

When the file is `main.tlc`, the symbol is `main.scale.spec.2`.

A different constant builds a different copy.

`name comptime []const u8` is that string in the copy.

An enumerator is a comptime argument on that same parameter.

`take(n, Order.acquire)` calls `take.spec.1` when `acquire` is `1`.

Each use of that name is `Order.acquire`.

A method and a generic function drop that parameter the same way.

When the file is `main.tlc` and `acquire` is `1`, `p.id(Order.acquire)` calls `main.Point.id.spec.1`, and `id[i32](1, Order.acquire)` calls `main.id.spec.i32.spec.1`.

Each use of that name is `Order.acquire`.

`comptime if` on that compare keeps one arm.

The other arm is not typechecked and not emitted.

An ordinary `if` typechecks both arms.

A `var` of the enum is `type mismatch`.

A function nested in another function does not take an enumerator.

A local `const` whose initializer folds to `i32` is the same specialization as that integer.

A `var` is `type mismatch`, except on `extern` and `extern(abi="tlc")`.

A function nested in another function still passes the constant.

Using the function as a value is `not supported yet`.

More than 32 nested specializations are `not supported yet`.

A struct, a float, an alias, a function, and a tuple are comptime arguments.

An `extern` function used as a value is the unspecialized function.

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

`return add(1)` folds to `1`.

`return scale(2, x)` passes `x` and calls `scale`, and `2` is an immediate.

## A comptime integer argument

A `comptime const` whose initializer folds to `i32` is that same argument.

`comptime const n = 2` then `return scale(n, x)` calls `main.scale.spec.2` when the file is `main.tlc`.

A runtime `const` and a `var` are `type mismatch`.

`scale(n + 1, x)` is `type mismatch`.

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

`n` is not stored.

The call passes `x` and adds `2`.

## A struct on a comptime function

A file-scope const of a struct is that argument.

The literal is the same value.

`const origin = Point{.x = 40, .y = 2}` then `return field(origin)` folds.

That `field` is `comptime func field(p Point) i32 { return p.x + p.y; }`.

There is no runtime symbol.

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

`field(Point{.x = 1, .y = 2})` folds the same way.

An enumerator passed to a `comptime func` folds when the body returns a constant `i32`.

`return rank(Order.acquire)` is `movl $42, %eax` and `retq`.

`type Integer i32` then `return id(Integer(4))` is `movl $4, %eax` and `retq`.

That `id` is `comptime func id(n Integer) i32 { return i32(n); }`.

A `var` passed to a `comptime func` is `not supported yet`.

A `var` of the enum is `not supported yet` on that call.

## A tuple on a comptime function

A tuple of comptime values folds the same way.

`return field((40, 2))` has no runtime symbol.

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

`p[0]` is `40` and `p[1]` is `2`.

A `var` passed to that function is `not supported yet`.

## A specialized struct parameter

A mix of a struct parameter and a runtime parameter stores the literal in the copy and drops the parameter.

When the file is `main.tlc`, `take(Point{.x = 2, .y = 0}, n)` calls `main.take.spec.p_2_0`.

`const origin = Point{.x = 2, .y = 0}` calls that same copy.

The fields are named in source order.

`Point{.y = 2, .x = 1}` calls `main.take.spec.p_2_1`.

`here.shift(Point{.x = 2, .y = 0})` calls `main.Point.shift.spec.p_2_0`.

That copy loads `self.x` and adds the stored `x` field.

A local `const` of a struct, a float, an alias, or a function is `type mismatch`.

A parameter `[2]i32` is a pointer.

`const row = [_]i32{1, 2}` is `type mismatch: expected *i32, found [2]i32`.

A function nested in another function passes the struct literal.

It does not take an enumerator.

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

`n` is stored.

The literal stores `x` as `2` and `y` as `0`.

The copy then loads both.

The add is `x + n`.

The call passes `n` and does not pass the struct.

## A specialized tuple parameter

A tuple is that argument too, and the copy does not store the tuple.

`p[0]` of `(2, 0)` is the immediate `2`.

When the file is `main.tlc`, `take((2, 0), n)` calls `main.take.spec.t_2_0`.

`here.shift((2, 0))` calls `main.Point.shift.spec.t_2_0` and loads `self.x` before adding `2`.

`id[i32](40, (2, 0))` calls `main.id.spec.i32.spec.t_2_0`.

A string element uses the spelling of a comptime string, so `("hi", 2)` calls `main.take.spec.t_s26869_2`.

When the tuple is the only parameter and the body returns `p[0]`, `return take((42, 0))` is `movl $42, %eax` and `retq`.

The compiler still compiles that function.

A `var` tuple is `type mismatch`.

A local `const` of a tuple is `type mismatch`.

A tuple element that is a local `const` is `type mismatch`.

An element that is a `var` is `type mismatch`.

### A bool element

A `bool` element stays `bool`.

When the file is `main.tlc`, `take((true, 2), n)` calls `main.take.spec.t_b1_2`.

`p[0]` is `true`, `i32(p[0])` is `1`, and `false` is `b0`.

`p[0] + 1` is `type mismatch`.

### A named tuple

`comptime const pair = (40, 2)` names that tuple, and `pair[0]` is that element.

`comptime const flags = (true, 1)` names that tuple, and `flags[0]` stays `bool`.

`if (flags[0])` and `flags[0] == true` use that value.

`flags[0] + 1` is `type mismatch`.

An element may be a `bool`, an `f64`, an `f32`, a struct, or a `[]const u8`.

An element may also be an enumerator, a tuple, a function, a pointer, or an array.

A runtime `const` of a tuple is `type mismatch`.

### A nested tuple

A tuple may be an element.

`comptime const pair = ((40, 2), 1)` names that tuple, `pair[0]` is `(40, 2)`, and `pair[0][1]` is `2`.

When the file is `main.tlc`, `take(((40, 2), 1), n)` calls `main.take.spec.t_t_40_2_1`.

The compiler does not pass the parameter, and `p[0][1]` is the immediate `2`.

`((true, 2), 1)` is `t_t_b1_2_1`, and `p[0][0]` stays `bool`.

`p[0][0] + 1` is `type mismatch`.

`null` and an element that is not constant are `type mismatch`.

A type parameter does not match a tuple inside a tuple.

A function nested in another function passes the tuple and does not specialize.

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

`n` is stored and increased by `2`.

The call passes `n` and does not pass the tuple.

## A function stored in a tuple

A function is that element.

`comptime const ops = (add, 1)` names that tuple.

The element is a function pointer, the same type as `comptime const op = add`.

`(ops[0])(40, 2)` calls `add`.

`ops[0](40, 2)` is a generic call of `ops`.

`(ops[0][0])(40, 2)` calls a function in a nested tuple.

When the file is `main.tlc`, `take((add, 1), n)` calls `main.take.spec.t_fnadd_1`.

`take(ops, n)` is that copy.

`comptime const op = add` then `(op, 1)` is `t_fnadd_1`.

`lone(ops[0], n)` calls `main.lone.spec.fnadd`, the same spelling as `take(add, n)`.

`((add, 1), 2)` is `t_t_fnadd_1_2`.

The compiler does not pass the parameter, and the copy calls `main.add`.

A `var` of that tuple is `type mismatch`.

A parameter that is not `comptime` is `type mismatch`.

`null` is `type mismatch`.

`var f = add` then `comptime const ops = (f, 1)` is `not supported yet`.

A function element is called through that pointer.

`(p[0])(n, 2)` is the call, and `p[0](n, 2)` is a generic call of `p`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func take(p comptime (func (i32, i32) i32, i32), n i32) i32 {
    return (p[0])(n, 2) + p[1];
}

func main() i32 {
    var n = 39;
    return take((add, 1), n);
}
```

```asm
add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

take.spec.t_fnadd_1:
movl %edi, 20(%rsp)              # move
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
movl $2, %esi                    # constant
callq main.add                   # call add
addl $1, %eax                    # add
retq                             # return

main:
movl $39, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.t_fnadd_1   # call take
retq                             # return
```

`n` is stored.

The copy calls `main.add` with `n` and `2`, then adds `1`.

The call passes `n` and does not pass the tuple.

## A pointer stored in a tuple

A pointer is that element when it is `&` of a `comptime const`.

`comptime const pair = (&addr, 2)` names that tuple, and `*pair[0]` is `addr`.

`comptime const p = &addr` then `(p, 2)` is the same pointer.

`pair[0].x` reads a field when the pointer addresses a struct.

`((&addr, 1), 2)` then `*nested[0][0]` is `addr`.

A local `comptime const` of that tuple is the same.

`comptime const n = *pair[0]` folds.

`return n + pair[1]` is `movl $42, %eax` and `retq` when `addr` is `40` and the other element is `2`.

A `comptime func` whose body is `return *value[0] + value[1]` folds the same way.

A `var` address, `comptime const pair = (&n, 2)`, is `not supported yet`.

`null` is `type mismatch`.

Passing the tuple to a `comptime` parameter is `not supported yet`.

There is no spelling for the pointer.

`&addr` alone on `p comptime *i32` is `type mismatch`.

A pointer element is `&` of a `comptime const`.

`*pair[0]` is that integer.

`comptime const n = *pair[0]` folds it.

```c
comptime const addr = 40;
comptime const pair = (&addr, 2);
comptime const n = *pair[0];

func main() i32 {
    return n + pair[1];
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`pair[0].x` reads a field when the pointer addresses a struct.

A `var` address is `not supported yet`.

Passing `(&addr, 2)` to a `comptime` parameter is `not supported yet`.

`&addr` alone on `p comptime *i32` is `type mismatch`.

## An array stored in a tuple

An array is that element.

`comptime const row = [_]i32{40, 2}` then `comptime const pair = (row, 0)` names that tuple.

The element stays `[2]i32`.

`pair[0][0]` is `40` and `pair[0][1]` is `2`.

`([_]i32{40, 2}, 0)` is the same tuple.

`((row, 1), 0)` then `nested[0][0][0]` is `40`.

A local `comptime const` of that tuple is the same.

`comptime const n = pair[0][0]` folds.

`return n + pair[0][1]` is `movl $42, %eax` and `retq`.

A `comptime func` whose body is `return value[0][0] + value[0][1]` folds the same way.

A `var` array in that tuple is `not supported yet`.

`pair[0][0] = 1` on that comptime const is `type mismatch`.

Passing the tuple to a `comptime` parameter is `not supported yet`.

There is no spelling for the array.

An array element stays `[2]i32`.

`pair[0][0]` is that integer.

`comptime const n = pair[0][0]` folds it.

```c
comptime const row = [_]i32{40, 2};
comptime const pair = (row, 0);
comptime const n = pair[0][0];

func main() i32 {
    return n + pair[0][1];
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`return pair[0][0] + pair[0][1]` is those same two instructions.

A `comptime func` whose body is `return value[0][0] + value[0][1]` folds the same way when the call is `use(pair)`.

### A nested array

A second index reads a nested array.

`pair[0][1][0]` is element 0 of row 1.

The sum below is 42, so `main` is that constant.

```c
comptime const grid = [_][2]i32{{40, 1}, {2, 3}};
comptime const pair = (grid, 0);
comptime const n = pair[0][1][0];

func main() i32 {
    return n + pair[0][0][0];
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

A `comptime func` folds the same indexes, and `pick(pair)` is `2 + 1`.

```c
comptime const grid = [_][2]i32{{40, 1}, {2, 3}};
comptime const pair = (grid, 0);

comptime func pick(value ([2][2]i32, i32)) i32 {
    return value[0][1][0] + value[0][0][1];
}

func main() i32 {
    return pick(pair);
}
```

```asm
main:
movl $3, %eax                    # constant
retq                             # return
```

### A runtime copy of the array

`var pair = (row, 0)` copies the array into the tuple.

`pair[0][0] = 1` stores in that copy, `row[0]` stays 40, and the sum is 41.

```c
comptime const row = [_]i32{40, 1};

func main() i32 {
    var pair = (row, 0);
    pair[0][0] = 1;
    return row[0] + pair[0][0];
}
```

```asm
main:
movl main.row(%rip), %eax        # row[0]
movl main.row+4(%rip), %ecx      # row[1]
movl %ecx, -12(%rsp)             # copy of row[1]
movl %eax, -16(%rsp)             # copy of row[0]
movl $0, -8(%rsp)                # pair's second element
movl $1, -16(%rsp)               # pair[0][0] = 1
movl main.row(%rip), %eax        # row[0]
addl -16(%rsp), %eax             # row[0] + pair[0][0]
retq                             # return
```

`var nested = ((row, 1), 0)` keeps the array in that copy.

`nested[0][0][0] = 2` stores there, `row[0]` stays 40, and the sum is 42.

```c
comptime const row = [_]i32{40, 1};

func main() i32 {
    var nested = ((row, 1), 0);
    nested[0][0][0] = 2;
    return row[0] + nested[0][0][0];
}
```

```asm
main:
movl main.row(%rip), %eax        # row[0]
movl main.row+4(%rip), %ecx      # row[1]
movl %ecx, -12(%rsp)             # copy of row[1]
movl %eax, -16(%rsp)             # copy of row[0]
movl $0, -4(%rsp)                # outer element
movl $1, -8(%rsp)                # inner element
movl $2, -16(%rsp)               # nested[0][0][0] = 2
movl main.row(%rip), %eax        # row[0]
addl -16(%rsp), %eax             # row[0] + nested[0][0][0]
retq                             # return
```

> **Warning:** These forms emit nothing.
>
> `comptime const n = row[0]` is `global initializer is not a constant`.
>
> A lone array index does not fold.
>
> `var row = [_]i32{40, 2}` then `comptime const pair = (row, 0)` is `not supported yet`.
>
> `take(pair, 0)` with `p comptime ([2]i32, i32)` is `not supported yet`.
>
> There is no spelling for the array.
>
> `pair[0][0] = 1` on a `comptime const pair` is `type mismatch`.

## A float argument

A float is that argument too.

`comptime if (v == 0.5)` keeps one arm.

When the file is `main.tlc`, `const scale = 0.5` and the literal `0.5` call `main.take.spec.d3fe0000000000000`.

The other arm is not emitted.

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

`n` is stored and increased by `2`.

The call passes `n` and does not pass `scale`.

## An alias or a function argument

An alias is that argument too.

`type Integer i32` then `const other = Integer(4)` on `n comptime Integer` calls `main.take.spec.4` when the file is `main.tlc`.

The copy adds `4`.

A function is that argument too.

`take(add, n)`, `const op = add`, `BinOp(add)`, and `const wrapped = BinOp(add)` call `main.take.spec.fnadd`.

That copy calls `main.add` with `n` and `2`.

## A comptime function as that argument

A `comptime func` is that argument too.

`take(add, n)` and `comptime const op = add` call `main.take.spec.fnadd` when the file is `main.tlc`.

That copy calls `main.add` with `n` and `2`.

The copy does not paste the body.

A direct call `add(40, 2)` still folds, with no trace frame.

```c
comptime func add(a i32, b i32) i32 {
    return a + b;
}

comptime const op = add;

func take(op comptime func (i32, i32) i32, n i32) i32 {
    return op(n, 2);
}

func main() i32 {
    var n = 40;
    return take(add, n) + take(op, n) - 42;
}
```

```asm
main.add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

main.take.spec.fnadd:
movl %edi, 20(%rsp)              # move
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
movl $2, %esi                    # constant
callq main.add                   # call add
retq                             # return

main:
movl $40, 36(%rsp)               # constant
movl 36(%rsp), %eax              # move
movl %eax, 24(%rsp)              # move
# trace frame for the call, then the matching pop
movl 24(%rsp), %edi              # move
callq main.take.spec.fnadd       # call take
movl 36(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.fnadd       # call take
movl 4(%rsp), %ecx               # move
movl 20(%rsp), %eax              # move
addl %ecx, %eax                  # add
subl $42, %eax                   # subtract
retq                             # return
```

`n` is stored.

Each call passes `n` and does not pass `add`.

The copy calls `main.add` with `n` and `2`.

The program returns 42.

> **Warning:** These forms emit nothing.
>
> `var f = add` when `add` is a `comptime func` is `not supported yet`.
>
> `BinOp(add)` when `add` is a `comptime func` is `not supported yet`.
>
> `(add, 1)` when `add` is a `comptime func` is `not supported yet`.
>
> `const op = add` when `add` is a `comptime func` and the `const` is not `comptime` is `not supported yet`.
>
> `var f = add` then `comptime const op = f` is `not supported yet`.

## An enumerator on an ordinary function

An enumerator is a comptime argument of that function.

The copy does not receive it.

The program returns 42.

When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.take.spec.1`.

`comptime if (order == Order.acquire)` keeps `return n + 2` and leaves the other arm out.

The other arm is not typechecked.

A method and a generic function do the same.

An ordinary `if` typechecks both arms.

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

`n` is stored, loaded, and increased by `2`.

The other arm is not emitted.

The call passes `n` and does not pass `order`.

## An enumerator on a method

A method drops the parameter the same way.

The program returns 42.

When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.Point.id.spec.1`.

`comptime if (order == Order.acquire)` keeps `return self.x + 41`.

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

`self.x` is loaded and increased by `41`.

The other arm is not emitted.

The call passes `p` and does not pass `order`.

## An enumerator on a generic function

A generic function drops the parameter after the type arguments.

The program returns 42.

When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.id.spec.i32.spec.1`.

The copy keeps `return value + 41`.

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

`value` is loaded and increased by `41`.

The call passes `1` and does not pass `order`.

## Extern keeps the unspecialized function

`extern` and `extern(abi="tlc")` keep the unspecialized function.

The comptime parameter is an argument of that symbol.

A dynamic linker can call that symbol.

When the file is `main.tlc`, the symbol is `main.scale`.

A constant argument uses the specialized copy.

A runtime argument uses `main.scale`.

A use of the function as a value uses `main.scale` too.

`extern` is the same declaration as `extern(abi="tlc")`.

`extern(abi="c")` passes the constant.

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

`return scale(2, x)` calls `main.scale.spec.2` and passes `x`.

`main.scale` adds its two arguments.

## A compile-time string

A file-scope `const text = "hi"` is a comptime string.

`len(text)` folds to `2`.

That `len` is `comptime func len(text []const u8) i32 { return text.len; }`.

A local `const text = "hi"` passed to that function is `not supported yet`.

## A comptime overload

A generic function may declare the same parameter list again with `comptime` on a parameter.

That overload is chosen when every `comptime` argument is a comptime value.

It is preferred over the overload without `comptime`.

A `var` does not choose it.

When only the `comptime` overload matches the types and an argument is not a comptime value, the call is `type mismatch`.

[`builtin.memeql`](../builtin/mem.md) and `builtin.memcmp` fold two constant `[]const u8` values through that overload.

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
