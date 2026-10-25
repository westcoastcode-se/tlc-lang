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

The call folds when the body is one `return` of a constant `i32`. There is no runtime symbol and no trace frame. `emit` is not valid in a `comptime func`. A `var` passed to a `comptime func` is `not supported yet`. A type parameter, a method, `extern`, `export`, and a call that does not fold are `not supported yet`. `comptime { ... }` inside an ordinary function is `not supported yet`. `comptime for` and `comptime if` are `not supported yet`. In a [macro](macro.md), `for` and `if` run while compiling without that prefix. `const for` is not a form.

An ordinary function may mark a parameter `comptime`. That argument has to be a comptime value, or the call is `type mismatch`. When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds. The function is still compiled.

A mix of a `comptime` parameter and a runtime parameter is specialized per constant. The comptime parameter is not passed. Each use of that name in the copy is the constant. `return scale(2, x)` calls that copy and adds `2`. When the file is `main.tlc`, the symbol is `main.scale.spec.2`. Another constant is another copy. `name comptime []const u8` is that string in the copy. A `var` is `type mismatch`, except on `extern` and `extern(abi="tlc")`. A method, and a function nested in another function, still pass the constant. Using the function as a value, a comptime argument that is neither a folded `i32` nor a constant `[]const u8`, and more than 32 nested specializations are `not supported yet`. An `extern` function used as a value is the unspecialized function.

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
