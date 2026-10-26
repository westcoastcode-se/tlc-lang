# The Type type

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`builtin.Type` is the type of a tlc type. It is the tagged union `Type` in `lib/tlc/builtin/builtin.tlc`. `TypeKind(value)` reads which arm is active. `Other` is every type that is not a function and not a struct: an integer, a pointer, an array, a slice, an enum, a tuple, a union, and an alias of a primitive. `Function` is a function. `has_this` is true when it has a receiver, including `self *this`. A free function and a static method have `has_this` false. `generic` is true when it has type parameters. `variadic` is true when the parameter list ends with `...`. `Struct` is a struct. `generic` is true when that struct is a specialization. `desc` holds the spelling and the fields. Another package cannot name `desc`. The compiler supplies the bodies of `name` and `fields`. `fields` returns `[]const Field`. `import "builtin"` names the union `builtin.Type`. `import { Type } from "builtin"` names it `Type`. From another package the tag is `builtin.TypeKind(value)` and an enumerator is `builtin.TypeKind.Function`. `typeof(i32)` and `typeof n` produce that type. A bare `Type`, with no such import, is reported as `not supported yet`. `typeof(Point)`, `typeof(*i32)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)`, `typeof(union Shape)`, and `typeof(trait Area)` are reported as `expected a type`. `typeof('A')` is `u8`, and that value is different from `typeof(i8)`. `typeof` does not decay an array. `typeof(T)` when `T` is `[4]i32` is that array. A parameter `[4]i32` is a pointer, so `typeof(value)` is `*i32`. `var t builtin.Type` with no value is reported as `type mismatch`. `typeof(builtin.Type).name()` spells `Type`. `sizeof(builtin.Type)` is 24.

`TypeKind(t) == TypeKind.Function && t.Function.has_this == false` reads `has_this` only when the arm is `Function`. `t.Struct.generic` is that struct's flag. `typeof(add)` for `func add(a i32, b i32) i32` has `has_this` false, `generic` false, and `variadic` false. `typeof(Point.get)` for an instance method has `has_this` true and spells `func(Point) i32`. `typeof(Point.ptr)` for `self *this` spells `func(*Point) i32`. `typeof(Point.make)` for a static method has `has_this` false and spells `func(i32) Point`. `typeof(id)` for `func id[T type](value T) T` spells `func[T type](T) T` and `generic` is true. `var f = id` is reported as `not supported yet`. `var f = Point.get` is reported as `type mismatch`. A function pointer is `Other`.

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*builtin.Type` uses the value `p` points at. `name` is a method of `builtin.Type`. `t.name()` returns `*const u8`. The text is the spelling diagnostics use: `i32`, `*i32`, `?*Point`, `[4]i32`, `Box.spec.i32`, `func(i32, i32) i32`, `*func(i32, i32) i32`. The bytes are constant. `*text = 0` and `text[0] = 0` are reported as `type mismatch`. `name` takes no arguments. Another argument is reported as `argument count`. Another method is reported as `undeclared identifier`. `t.name` without the call is reported as `undeclared identifier`.

`t.fields()` returns the fields of a named struct, in the order they are written. The result is `[]const Field`. Assigning `fields[i]`, or a field of that element, is `type mismatch`. `fields.len` is how many there are. `fields[i].name` is that field's name as `[]const u8`. The length is the number of bytes in the spelling. It converts to `*const u8` where that pointer is expected. `fields[i].kind` is `*const builtin.Type`. `*fields[i].kind` is that field's type. `fields[i].kind.name()` spells it and `fields[i].kind.fields()` reads a struct field's own fields. Storing through `fields[i].kind` is `type mismatch`. The field is `kind` because `type` is a keyword. An alias of a struct lists that struct's fields. `typeof(Alias)` stays different from `typeof(Point)`. A generic struct lists the fields of that specialization. When `box` is `Box[i32]`, `typeof(box)` has `value` of type `i32`. Any other type has length 0, including `i32`, a pointer, an array, a slice, a function, an alias of a primitive, an anonymous struct, and a union. `fields` takes no arguments. Another argument is reported as `argument count`. `t.fields` without the call is reported as `undeclared identifier`. `p.fields()` on a `*builtin.Type` uses the value `p` points at.

`==` and `!=` compare two `builtin.Type` values and yield `bool`. The compare uses the spelling and the flags. A method and a free function with the same parameters are not equal, because `has_this` differs. Other operators on `builtin.Type` are reported as `type mismatch`.

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
leaq (%rip), %r8                 # address
movq %r8, -48(%rsp)              # move
movl $0, -32(%rsp)               # constant
movb $0, -38(%rsp)               # constant
movb $0, -39(%rsp)               # constant
movb $0, -40(%rsp)               # constant
movq -48(%rsp), %rax             # move
movq (%rax), %rax                # move
movq %rax, -24(%rsp)             # move
movq (%rip), %rax                # move
movl (%rip), %ecx                # move
movl %ecx, -8(%rsp)              # move
movq %rax, -16(%rsp)             # move
movl -32(%rsp), %eax             # move
movb -38(%rsp), %dl              # move
movb -39(%rsp), %sil             # move
movb -40(%rsp), %dil             # move
movq -48(%rsp), %rcx             # move
subq %r8, %rcx                   # subtract
sete %cl                         # flag to byte
xorb $1, %dil                    # xor
andb %dil, %cl                   # and
xorb $1, %sil                    # xor
andb %sil, %cl                   # and
xorb $1, %dl                     # xor
andb %dl, %cl                    # and
cmpl $0, %eax                    # compare
sete %al                         # flag to byte
andb %al, %cl                    # and
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -49(%rsp)              # move
jne .L1                          # branch
jmp .L2                          # jump
.L1:
movb $1, %al                     # constant
movb %al, -49(%rsp)              # move
jmp .L2                          # jump
.L2:
movb -49(%rsp), %cl              # move
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -50(%rsp)              # move
jne .L3                          # branch
jmp .L4                          # jump
.L3:
cmpq $0, -24(%rsp)               # compare
setne %al                        # flag to byte
movb %al, -50(%rsp)              # move
.L4:
movb -50(%rsp), %cl              # move
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -51(%rsp)              # move
jne .L5                          # branch
jmp .L6                          # jump
.L5:
movl -8(%rsp), %eax              # move
movq -16(%rsp), %rcx             # move
cmpl $2, %eax                    # compare
sete %al                         # flag to byte
movb %al, -51(%rsp)              # move
.L6:
movb -51(%rsp), %cl              # move
xorl %eax, %eax                  # zero the result
testb $1, %cl                    # test
movb %al, -52(%rsp)              # move
jne .L7                          # branch
jmp .L8                          # jump
.L7:
movq -16(%rsp), %rax             # move
movq 16(%rax), %rax              # move
movl 16(%rax), %ecx              # move
movb 10(%rax), %dl               # move
movb 9(%rax), %sil               # move
movb 8(%rax), %dil               # move
movq (%rax), %rax                # move
leaq (%rip), %r8                 # address
subq %r8, %rax                   # subtract
sete %al                         # flag to byte
xorb $1, %dil                    # xor
andb %dil, %al                   # and
xorb $1, %sil                    # xor
andb %sil, %al                   # and
xorb $1, %dl                     # xor
andb %dl, %al                    # and
cmpl $0, %ecx                    # compare
sete %cl                         # flag to byte
andb %cl, %al                    # and
movb %al, -52(%rsp)              # move
.L8:
movb -52(%rsp), %al              # move
testb $1, %al                    # test
jne .L9                          # branch
jmp .L10                         # jump
.L9:
movl $1, %eax                    # constant
retq                             # return
.L10:
jmp .L11                         # jump
.L11:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
