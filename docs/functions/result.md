# Several results

[Index](../README.md) · [Previous: Functions](function.md) · [Next: Nested functions](nested.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

The result list is parenthesized, and each result is a type. One type in parentheses is the same as writing that type alone. `return` lists one expression per result.

```c
func add(x i32, y i32) (i32, i32) {
    return x + y, x;
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $20, %edi                   # constant
movl $22, %esi                   # constant
callq add                        # call add
retq                             # return

add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %edx              # move
movl -4(%rsp), %ecx              # move
movl %edx, %eax                  # move
addl %ecx, %eax                  # add
subl %ecx, %edx                  # subtract
retq                             # return
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

```asm
main:
# trace frame for the call, then the matching pop
movl $20, %edi                   # constant
movl $22, %esi                   # constant
callq add                        # call add
retq                             # return

add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %edx              # move
movl -4(%rsp), %ecx              # move
movl %edx, %eax                  # move
addl %ecx, %eax                  # add
retq                             # return
```

`value[0]` reads one element. The index is a constant, so the element type and size are known while compiling, including when the elements differ, as in `(40, 'a')[1]`. `value[i]` when `i` is not a constant is `index is not a compile-time constant`. An index past the last element is `type mismatch`. `value[0] = 1` stores into that field of a tuple variable. A temporary, `(40, 2)[0] = 1`, and a `const` tuple are `not an lvalue`. `value.len` is `i32` and is the number of elements. It is a constant. `(40, 'a').len` is 2. `value.len = 1` is `not an lvalue`.

`((i32, i32))` is one tuple result, not two results. `return (40, 2)` matches that tuple. `return 40, 2` does not. One value that is not that tuple is `return type: expected (i32, i32), found i32`. `return Duo((40, 2))` is `type mismatch`, because `Duo` is a different type. `((i32, i32), i32)` is that tuple beside another result. `return (40, 2), 0` matches it. `return 40, 2, 0` is `return type: expected 2 results, found 3`. `return Duo((40, 2)), 0` is `type mismatch`. `var both = pair()` binds the call, and `both[0][0]` reads an element of the tuple. `func () ((i32, i32))` is that tuple as a function type. `var op func () ((i32, i32)) = pair` stores the function, `var value = op()` binds the tuple, and `value[0] + value[1]` is 42. `func () (i32, i32)` is still two results. `func () ((i32, i32), i32)` is that tuple beside another result. `var op func () ((i32, i32), i32) = pair` stores the function, `var value, n = op()` unpacks the call, and `value[0] + value[1] + n` is 42. `func ((i32, i32)) i32` is one tuple parameter. `var op func ((i32, i32)) i32 = take` stores `take` when `take` takes `(i32, i32)`, and `op((40, 2))` calls it. `op(40, 2)` is `argument count`. `func (i32, i32) i32` is still two parameters. A function of two `i32` parameters does not match, and that assignment is `type mismatch`. `op(Duo((40, 2)))` is `type mismatch`, because `type Duo (i32, i32)` is a different type. `func ((i32, i32), i32) i32` is that tuple beside another parameter. `var op func ((i32, i32), i32) i32 = take` stores `take` when `take` takes `(i32, i32)` and an `i32`, and `op((40, 1), 1)` calls it. `func (i32, (i32, i32)) i32` is that tuple as another parameter. `op((40, 1))` is `argument count`. `op(40, 2)` is `type mismatch`. A function of three `i32` parameters does not match, and that assignment is `type mismatch`. `op(Duo((40, 1)), 1)` is `type mismatch`, because `type Duo (i32, i32)` is a different type.

```c
func pair() ((i32, i32)) {
    return (40, 2);
}

func main() i32 {
    var value = pair();
    return value[0] + value[1];
}
```

```asm
pair:
movl $40, %eax                   # constant
movl $2, %edx                    # constant
retq                             # return
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

```asm
pair:
movl $40, %eax                   # constant
movl $2, %edx                    # constant
xorl %ecx, %ecx
retq                             # return
```

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

`var` defines every name in the current block. `:=` defines a name that is new in the current block and assigns a name that already exists there. At least one name must be new. A name from an outer block is shadowed. The same bind may be the first clause of `for`. A call used as a statement, or as the call in `defer`, discards every result. The call may be a function, a method on a field, a subscript, a dereference, or a temporary value. An `extern(abi="c")` function cannot return several results. A type error names the expected type and the found type.

[Index](../README.md) · [Previous: Functions](function.md) · [Next: Nested functions](nested.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
