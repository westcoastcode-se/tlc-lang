# Several results

[Index](../README.md) · [Previous: Functions](function.md) · [Next: Nested functions](nested.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

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

`value[0]` reads one element. The index is a constant, so the element type and size are known while compiling, including when the elements differ, as in `(40, 'a')[1]`. `value[i]` when `i` is not a constant is `not supported yet`. An index past the last element is `type mismatch`. `value[0] = 1` stores into that field of a tuple variable. A temporary, `(40, 2)[0] = 1`, and a `const` tuple are `not an lvalue`. `value.len` is `i32` and is the number of elements. It is a constant. `(40, 'a').len` is 2. `value.len = 1` is `not an lvalue`.

`((i32, i32))` is one tuple result, not two results. `return (40, 2)` matches that tuple. `return 40, 2` does not. One value that is not that tuple is `return type: expected (i32, i32), found i32`. `return Duo((40, 2))` is `type mismatch`, because `Duo` is a different type. `((i32, i32), i32)` is that tuple beside another result. `return (40, 2), 0` matches it. `return 40, 2, 0` is `return type: expected 2 results, found 3`. `return Duo((40, 2)), 0` is `type mismatch`. `var both = pair()` binds the call, and `both[0][0]` reads an element of the tuple. `func () ((i32, i32))` is that tuple as a function type. `var op func () ((i32, i32)) = pair` stores the function, `var value = op()` binds the tuple, and `value[0] + value[1]` is 42. `func () (i32, i32)` is still two results. `func () ((i32, i32), i32)` is `not supported yet`.

```c
func pair() ((i32, i32)) {
    return (40, 2);
}

func main() i32 {
    var value = pair();
    return value[0] + value[1];
}
```

```c
func pair() ((i32, i32), i32) {
    return (40, 2), 0;
}

func main() i32 {
    var value, n = pair();
    return value[0] + value[1] + n;
}
```

`var` defines every name in the current block. `:=` defines a name that is new in the current block and assigns a name that already exists there. At least one name must be new. A name from an outer block is shadowed. The same bind may be the first clause of `for`. A call used as a statement, or as the call in `defer`, discards every result. The call may be a function, a method on a field, a subscript, a dereference, or a temporary value. An `extern(abi="c")` function cannot return several results. A type error names the expected type and the found type.

## Cost

`return x + y, x` for two `i32` parameters is 7 instructions. The sum is in `%eax` and the second result is in `%edx`. `t[0] = 1` stores into that field at a fixed offset. A non-constant index adds no instructions. `return t.len` on `var t = (40, 2)` is 2 instructions: `movl` of 2 and `retq`. The tuple is not stored.

The caller of `a, b = pair(20, 22)` pays the traced call and then 2 stores from those registers into `a` and `b`. With both locals declared as `var a i32 = 0` and `var b i32 = 0`, `main` executes 45 instructions on the path where the trace holds fewer than 32 frames. A call used as a statement still calls, and it does not store the results.

`return (40, 2)` from `func pair() ((i32, i32))` is 3 instructions: `movl $40, %eax`, `movl $2, %edx`, and `retq`. `func pair() (i32, i32) { return 40, 2; }` is those same 3 instructions. `var value = pair(); return value[0] + value[1];` executes 43 instructions in `main` on the path where the trace holds fewer than 32 frames, the same as binding that two-result call and indexing it. `return sizeof(pair())` is 2 instructions: `movl $8, %eax` and `retq`. `return (40, 2), 0` from `func pair() ((i32, i32), i32)` is 4 instructions: `movl $40, %eax`, `movl $2, %edx`, `xorl %ecx, %ecx`, and `retq`. `var value, n = pair(); return value[0] + value[1] + n;` executes 47 instructions in `main` on that same path. `return sizeof(pair())` for that result is 2 instructions: `movl $12, %eax` and `retq`. `var op func () ((i32, i32)) = pair; var value = op(); return value[0] + value[1];` executes 48 instructions in `main` on that path. The callee is those same 3 instructions.

[Index](../README.md) · [Previous: Functions](function.md) · [Next: Nested functions](nested.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
