# Comptime

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A `comptime func` calculates while compiling. Every argument is a comptime value, whether or not the parameter is marked `comptime`. A comptime value is a folded integer or a constant `[]const u8`: a literal, a constant range, or a file-scope `const` in the same file. A `var` and a local `const` are not. `return` leaves the function. The call is an expression. It does not paste the body into the caller.

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

The call folds when the body is one `return` of a constant `i32`. There is no runtime symbol and no trace frame. `emit` is not valid in a `comptime func`. A `var` passed to a `comptime func` is `not supported yet`. A type parameter, a method, `extern`, `export`, and a call that does not fold are `not supported yet`. `comptime { ... }` inside an ordinary function is `not supported yet`. `comptime for` is `not supported yet`. `comptime if` keeps one arm when the condition folds. A condition that does not fold is `not supported yet`. An ordinary `if` does the same when every value in the condition is comptime. A `var` and a local `const` leave both arms. In a [macro](macro.md), `comptime if` is `not supported yet`, and `for` and `if` run while compiling without that prefix. `const for` is not a form.

An ordinary function may mark a parameter `comptime`. That argument has to be a comptime value, or the call is `type mismatch`. When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds. The function is still compiled.

A mix of a `comptime` parameter and a runtime parameter is specialized per constant. The comptime parameter is not passed. Each use of that name in the copy is the constant. `return scale(2, x)` calls that copy and adds `2`. When the file is `main.tlc`, the symbol is `main.scale.spec.2`. Another constant is another copy. `name comptime []const u8` is that string in the copy. An enumerator is that argument too. `take(n, Order.acquire)` calls `take.spec.1` when `acquire` is `1`. Each use of that name is `Order.acquire`. A method and a generic function take an enumerator and still pass that parameter. The generic copy keeps it. A `var` of the enum is `type mismatch`. A function nested in another function does not take an enumerator. A `var` is `type mismatch`, except on `extern` and `extern(abi="tlc")`. A method, and a function nested in another function, still pass the constant. Using the function as a value, a comptime argument that is neither a folded `i32`, a constant `[]const u8`, nor an enumerator, and more than 32 nested specializations are `not supported yet`. An `extern` function used as a value is the unspecialized function.

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

An enumerator is a comptime argument of that function. The copy does not receive it. The program returns 42. When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.take.spec.1`. `order == Order.acquire` folds, so the copy keeps `return n + 2` and leaves the other arm out. A method still passes `order`, and a generic copy keeps it, so that `if` stays a runtime compare and both arms are typechecked.

```c
type Order enum {
    relaxed,
    acquire,
};

func take(n i32, order comptime Order) i32 {
    if (order == Order.acquire) {
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
