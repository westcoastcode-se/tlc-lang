# The Type type

[Index](../README.md) · [Previous: Slices](slice.md) · [Next: Structs](struct.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`builtin.Type` is the type of a tlc type. It is `*const builtin.CompiledType`.

`typeof` returns that pointer. The union is not copied.

## The union

`builtin.CompiledType` is the tagged union in `lib/tlc/builtin/builtin.tlc`. `TypeKind(*value)` reads which arm is active.

`Other` is every type that is not a function and not a struct: an integer, a pointer, an array, a slice, an enum, a tuple, a union, and an alias of a primitive.

`Function` is a function. `has_this` is true when it has a receiver, including `self *this`. A free function and a static method have `has_this` false.

`generic` is true when it has type parameters. `variadic` is true when the parameter list ends with `...`.

`Struct` is a struct. `generic` is true when that struct is a specialization. `desc` holds the spelling and the fields. Another package cannot name `desc`.

The compiler supplies the bodies of `name` and `fields`. `fields` returns `[]const Field`.

`import "builtin"` names the pointer `builtin.Type` and the union `builtin.CompiledType`. `import { Type } from "builtin"` names the pointer `Type`. From another package the tag is `builtin.TypeKind(*value)` and an enumerator is `builtin.TypeKind.Function`.

`sizeof(builtin.Type)` is 8. `sizeof(builtin.CompiledType)` is 24.

## `typeof`

`typeof(i32)` and `typeof n` produce that pointer. A bare `Type`, with no such import, is reported as `not supported yet`.

`typeof(Point)`, `typeof(*i32)`, and `typeof(enum Color)` name a declared type. `typeof(struct Point)`, `typeof(union Shape)`, and `typeof(trait Area)` are reported as `expected a type`.

`typeof('A')` is `u8`, and that value is different from `typeof(i8)`.

`typeof` does not decay an array. `typeof(T)` when `T` is `[4]i32` is that array. A parameter `[4]i32` is a pointer, so `typeof(value)` is `*i32`.

`var t builtin.Type` with no value is reported as `type mismatch`. `typeof(builtin.Type).name()` spells `Type`.

## Arms

`TypeKind(*t) == TypeKind.Function && t.Function.has_this == false` reads `has_this` only when the arm is `Function`. `t.Struct.generic` is that struct's flag. `TypeKind(t)` is `type mismatch`, because `t` is a pointer.

`typeof(add)` for `func add(a i32, b i32) i32` has `has_this` false, `generic` false, and `variadic` false. `typeof(Point.get)` for an instance method has `has_this` true and spells `func(Point) i32`.

`typeof(Point.ptr)` for `self *this` spells `func(*Point) i32`. `typeof(Point.make)` for a static method has `has_this` false and spells `func(i32) Point`.

`typeof(id)` for `func id[T type](value T) T` spells `func[T type](T) T` and `generic` is true. `var f = id` is reported as `not supported yet`. `var f = Point.get` is reported as `type mismatch`.

A function pointer is `Other`.

## `name`

`typeof(n).name()` calls `name`. `typeof n.x` is the type of the field. `p.name()` on a `*builtin.Type` uses the `Type` `p` points at.

`name` is a method of `builtin.Type`. `t.name()` returns `*const u8`. The text is the spelling diagnostics use: `i32`, `*i32`, `?*Point`, `[4]i32`, `Box.spec.i32`, `func(i32, i32) i32`, `*func(i32, i32) i32`.

The bytes are constant. `*text = 0` and `text[0] = 0` are reported as `type mismatch`.

`name` takes no arguments. Another argument is reported as `argument count`. Another method is reported as `undeclared identifier`. `t.name` without the call is reported as `undeclared identifier`.

## `fields`

`t.fields()` returns the fields of a named struct, in the order they are written. The result is `[]const Field`.

Assigning `fields[i]`, or a field of that element, is `type mismatch`. `fields.len` is how many there are.

`fields[i].name` is that field's name as `[]const u8`. The length is the number of bytes in the spelling. It converts to `*const u8` where that pointer is expected.

`fields[i].kind` is `builtin.Type`. `fields[i].kind == typeof(i32)` compares those pointers. `fields[i].kind.name()` spells the field's type and `fields[i].kind.fields()` reads a struct field's own fields.

Storing through `fields[i].kind` is `type mismatch`. The field is `kind` because `type` is a keyword.

An alias of a struct lists that struct's fields. `typeof(Alias)` stays different from `typeof(Point)`.

A generic struct lists the fields of that specialization. When `box` is `Box[i32]`, `typeof(box)` has `value` of type `i32`.

Any other type has length 0, including `i32`, a pointer, an array, a slice, a function, an alias of a primitive, an anonymous struct, and a union.

`fields` takes no arguments. Another argument is reported as `argument count`. `t.fields` without the call is reported as `undeclared identifier`. `p.fields()` on a `*builtin.Type` uses the `Type` `p` points at.

## Comparison

`==` and `!=` compare two `builtin.Type` pointers and yield `bool`. Two types are equal when the spelling and the flags are the same.

A method and a free function with the same parameters are not equal, because `has_this` differs. Other operators on `builtin.Type` are reported as `type mismatch`.

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
        fields[0].kind == typeof(i32)) {
        return 1;
    }
    return 0;
}
```

```asm
main:
leaq (%rip), %rax                # typeof(n)
movq %rax, -32(%rsp)             # t
movq -32(%rsp), %rax             # t
movq (%rax), %rax                # desc
movq (%rax), %rax                # name
movq %rax, -24(%rsp)             # text
movq (%rip), %rcx                # fields
movq 8(%rcx), %rax               # fields data
movl 16(%rcx), %ecx              # fields.len
movl %ecx, -8(%rsp)              # len
movq %rax, -16(%rsp)             # data
xorl %eax, %eax                  # zero the result
leaq (%rip), %rcx                # typeof(i32)
cmpq %rcx, -32(%rsp)             # t == typeof(i32)
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
cmpq $0, -24(%rsp)               # text != null
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
movl -8(%rsp), %eax              # fields.len
movq -16(%rsp), %rcx             # fields
cmpl $2, %eax                    # len == 2
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
movq -16(%rsp), %rax             # fields
leaq (%rip), %rcx                # typeof(i32)
cmpq %rcx, 16(%rax)              # fields[0].kind
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
