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

An ordinary function may mark a parameter `comptime`. That argument has to be a comptime value, or the call is `type mismatch`. When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds. The function is still compiled. A mix of `comptime` and runtime parameters keeps the call and passes the comptime argument as that constant.

```c
func add(n comptime i32) i32 {
    return n;
}

func scale(n comptime i32, x i32) i32 {
    return x + n;
}
```

```asm
scale:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl -8(%rsp), %eax              # add
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

`return add(1)` folds to `1`. `return scale(2, x)` passes `2` and calls `scale`.

A file-scope `const text = "hi"` is a comptime string. `len(text)`, when `len` is `comptime func len(text []const u8) i32 { return text.len; }`, folds to `2`. A local `const text = "hi"` passed to that function is `not supported yet`.

A generic function may declare the same parameter list again with `comptime` on a parameter. That overload is chosen when every `comptime` argument is a comptime value, and it is preferred over the overload without `comptime`. A `var` does not choose it. When only the `comptime` overload matches the types and an argument is not a comptime value, the call is `type mismatch`. [`builtin.memeql`](../builtin/mem.md) and `builtin.memcmp` fold two constant `[]const u8` values through that overload.

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
