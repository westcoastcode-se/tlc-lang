# Type values

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`typeof(i32)` and `typeof n` have type `builtin.Type`. `import "builtin"` names that type. `==` and `!=` compare two `builtin.Type` values and yield `bool`. Two `typeof` expressions are constants, so `assert(typeof(i32) == typeof(i32))` folds while compiling. `t.name()` returns the diagnostic spelling as `*const u8`. A store through that pointer is `type mismatch`. `t.fields()` returns `[]const Field`, the fields of a named struct. A tuple is that same list, one element per value, and `fields[i].name` is the index `"0"`, `"1"`, and so on. `typeof(("hi", 26)).name()` spells `([]const u8, i32)`. Assigning an element is `type mismatch`. `fields[i].name` is the field's name, a `[]const u8`, and `fields[i].kind` is `*const builtin.Type`. `*fields[i].kind` is its type. Storing through `fields[i].kind` is `type mismatch`. `typeof(T)` when `T` is an array is that array, the same value as `typeof([4]i32)`. `typeof(*T)` when `T` is an array, and `typeof([4]T)`, are `not supported yet`. `typeof` of a function or a trait is `not supported yet`. Other operators on `builtin.Type` are reported as `type mismatch`.

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
leaq (%rip), %rax                # address
movq %rax, -8(%rsp)              # move
xorl %eax, %eax                  # zero the result
movb $1, %cl                     # constant
testb $1, %cl                    # test
movb %al, -9(%rsp)               # move
jne .L1                          # branch
jmp .L2                          # jump
.L1:
movq -8(%rsp), %rax              # move
cmpq $0, (%rax)                  # compare
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

[Index](../README.md) · [Previous: Conditions](condition.md) · [Next: Casts](cast.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
