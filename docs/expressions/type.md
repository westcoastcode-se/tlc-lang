# Type values

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes `typeof`, which yields a `builtin.Type` you can compare and inspect.

## Comparing two types

`typeof(i32)` and `typeof n` have type `builtin.Type`, and `import "builtin"` names that type.

`==` and `!=` compare two `builtin.Type` pointers and yield `bool`.

Two `typeof` expressions are constants, so `assert(typeof(i32) == typeof(i32))` folds while compiling.

## The name and the fields

`t.name()` returns the diagnostic spelling as `*const u8`, and a store through that pointer is `type mismatch`.

`t.fields()` returns `[]const Field`, the fields of a named struct.

A tuple is that same list, one element per value, and `fields[i].name` is the index `"0"`, `"1"`, and so on.

`typeof(("hi", 26)).name()` spells `([]const u8, i32)`.

Assigning an element is `type mismatch`.

`fields[i].name` is the field's name, a `[]const u8`, and `fields[i].kind` is `builtin.Type`.

`fields[i].kind` is that field's type, and storing through `fields[i].kind` is `type mismatch`.

## Arrays

`typeof(T)` when `T` is an array is that array, the same value as `typeof([4]i32)`.

`typeof(*T)` when `T` is an array, and `typeof([4]T)`, are `not supported yet`.

## Functions

`typeof(add).name()` spells `func(i32, i32) i32` when `add` takes two `i32` values and returns `i32`.

`typeof(id).name()` spells `func(i32) i32`, and `typeof(none).name()` spells `func() void` when `none` returns nothing.

`typeof(pair).name()` spells `func(i32) (i32, i32)` when `pair` returns two `i32` values.

`typeof(logf).name()` spells `func(*const u8, ...) i32` when `logf` is `extern(abi="c")` and variadic.

Two functions compare equal when the spelling and the flags are the same.

A method and a free function with the same parameters are not equal.

`var f = add` has type `*func(i32, i32) i32`, and `typeof(f).name()` spells that.

`typeof(add).fields().len` is 0, and `return typeof(add).fields().len` is `xorl %eax, %eax` and `retq`.

`typeof(id)` for `func id[T type](value T) T` spells `func[T type](T) T` and `generic` is true.

`var f = id` is `not supported yet`, and `var f = Point.get` is `type mismatch`.

`typeof(trait Name)` is `expected a type`.

`sizeof(add)` is `not supported yet`.

Other operators on `builtin.Type` are reported as `type mismatch`.

```c
import "builtin";

func main() i32 {
    var n i32 = 1;
    var t builtin.Type = typeof(n);
    if (typeof(n) == typeof(i32) && t.name() != null) {
        return 1;
    }
    return 0;
}
```

```asm
main:
leaq (%rip), %rax                # typeof(n)
movq %rax, -8(%rsp)              # t
xorl %eax, %eax                  # zero the result
movb $1, %cl                     # same type
testb $1, %cl                    # test
movb %al, -9(%rsp)               # move
jne .L1                          # branch
jmp .L2                          # jump
.L1:
movq -8(%rsp), %rax              # t
movq (%rax), %rax                # desc
cmpq $0, (%rax)                  # name != null
setne %al                        # flag to byte
movb %al, -9(%rsp)               # move
.L2:
movb -9(%rsp), %al               # move
testb $1, %al                    # test
jne .L3                          # branch
jmp .L4                          # jump
.L3:
movl $1, %eax                    # constant
retq                             # return
.L4:
jmp .L5                          # jump
.L5:
xorl %eax, %eax                  # zero the result
retq                             # return
```

```c
func add(a i32, b i32) i32 { return a + b; }
func sub(a i32, b i32) i32 { return a - b; }

func main() i32 {
    return i32(typeof(add) == typeof(sub));
}
```

```asm
main:
movl $1, %eax                    # constant
retq                             # return
```

The program returns 1.

`add` and `sub` are not called.

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
