# The Type type

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`builtin.Type` is the type of a tlc type. `name`, `fields`, and the struct `Field` are declared in `lib/tlc/builtin/builtin.tlc`. `Type` is not a typedef there. The compiler adds that export. `fields` returns `[]const Field`. `import "builtin"` names it `builtin.Type`. `import { Type } from "builtin"` names it `Type`. `typeof(i32)` and `typeof n` produce that type. A bare `Type`, with no such import, is reported as `not supported yet`. `typeof(Point)`, `typeof(*i32)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)`, `typeof(union Shape)`, and `typeof(trait Area)` are reported as `expected a type`. `typeof('A')` is `u8`, and that value is different from `typeof(i8)`. `typeof` does not decay an array. `var t builtin.Type` with no value is reported as `type mismatch`. `typeof(builtin.Type).name()` spells `Type`.

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*builtin.Type` uses the value `p` points at. `name` is a method of `builtin.Type`. `t.name()` returns `*const u8`. The text is the spelling diagnostics use: `i32`, `*i32`, `?*Point`, `[4]i32`, `Box.spec.i32`. The bytes are constant. `*text = 0` and `text[0] = 0` are reported as `type mismatch`. `name` takes no arguments. Another argument is reported as `argument count`. Another method is reported as `undeclared identifier`. `t.name` without the call is reported as `undeclared identifier`.

`t.fields()` returns the fields of a named struct, in the order they are written. The result is `[]const Field`. Assigning `fields[i]`, or a field of that element, is `type mismatch`. `fields.len` is how many there are. `fields[i].name` is that field's name as `[]const u8`. The length is the number of bytes in the spelling. It converts to `*const u8` where that pointer is expected. `fields[i].kind` is `*const builtin.Type`. `*fields[i].kind` is that field's type. `fields[i].kind.name()` spells it and `fields[i].kind.fields()` reads a struct field's own fields. Storing through `fields[i].kind` is `type mismatch`. The field is `kind` because `type` is a keyword. An alias of a struct lists that struct's fields. `typeof(Alias)` stays different from `typeof(Point)`. A generic struct lists the fields of that specialization: `typeof(Box[i32])` has `value` of type `i32`. Any other type has length 0, including `i32`, a pointer, an array, a slice, an alias of a primitive, an anonymous struct, and a union. `fields` takes no arguments. Another argument is reported as `argument count`. `t.fields` without the call is reported as `undeclared identifier`. `p.fields()` on a `*builtin.Type` uses the value `p` points at.

`==` and `!=` compare two `builtin.Type` values and yield `bool`. Other operators on `builtin.Type` are reported as `type mismatch`.

```c
import "builtin";

type Point struct {
    x i32;
    y i32;
};

func main() i32 {
    var n i32 = 1;
    var t builtin.Type = typeof(n);
    var text *const u8 = t.name();
    var fields = typeof(Point).fields();
    if (t == typeof(i32) && typeof('A') == typeof(u8) && text != null && fields.len == 2 &&
        *fields[0].kind == typeof(i32)) {
        return 1;
    }
    return 0;
}
```

```asm
main:
leaq (%rip), %rax                # address
movq %rax, -32(%rsp)             # move
movq -32(%rsp), %rax             # move
movq (%rax), %rax                # move
movq %rax, -24(%rsp)             # move
movq (%rip), %rax                # move
movl (%rip), %ecx                # move
movl %ecx, -8(%rsp)              # move
movq %rax, -16(%rsp)             # move
xorl %eax, %eax                  # zero the result
leaq (%rip), %rcx                # address
cmpq %rcx, -32(%rsp)             # compare
movb %al, -33(%rsp)              # move
jne .L1                          # branch
movb $1, %al                     # constant
movb %al, -33(%rsp)              # move
jmp .L1                          # jump
.L1:
movb -33(%rsp), %cl              # move
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -34(%rsp)              # move
jne .L2                          # branch
jmp .L3                          # jump
.L2:
cmpq $0, -24(%rsp)               # compare
setne %al                        # flag to byte
movb %al, -34(%rsp)              # move
.L3:
movb -34(%rsp), %cl              # move
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -35(%rsp)              # move
jne .L4                          # branch
jmp .L5                          # jump
.L4:
movl -8(%rsp), %eax              # move
movq -16(%rsp), %rcx             # move
cmpl $2, %eax                    # compare
sete %al                         # flag to byte
movb %al, -35(%rsp)              # move
.L5:
movb -35(%rsp), %cl              # move
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -36(%rsp)              # move
jne .L6                          # branch
jmp .L7                          # jump
.L6:
movl -8(%rsp), %eax              # move
movq -16(%rsp), %rax             # move
movq 16(%rax), %rax              # move
leaq (%rip), %rcx                # address
cmpq %rcx, (%rax)                # compare
sete %al                         # flag to byte
movb %al, -36(%rsp)              # move
.L7:
movb -36(%rsp), %al              # move
testb $1, %al                    # test
jne .L8                          # branch
jmp .L9                          # jump
.L8:
movl $1, %eax                    # constant
retq                             # return
.L9:
jmp .L10                         # jump
.L10:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
