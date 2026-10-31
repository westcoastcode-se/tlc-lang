# string

[Index](../README.md) · [Previous: Strings](../types/string.md) · [Next: unicode](unicode.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes the C-compatible byte view `byte_string_view` and the owned `byte_string`. Unicode text is [`unicode.string`](unicode.md). ASCII text is [`ascii.string`](ascii.md).

## The package

`import "std/string"` loads `lib/tlc/std/string`.

The package is ordinary tlc source.

The compiler does not implement these functions.

## What a `byte_string_view` is

`string.byte_string_view` is `[]const u8`. `"hello"b` has that type. `string.byte_string` owns those bytes.

`byte_string_view` does not convert back from the alias by itself. A cast is that conversion.

`byte_string_view` converts to `*const u8` by passing the address and dropping the length.

Any other `[]const u8` converts to `*const u8` the same way.

`string.from` returns a `byte_string_view`.

The parameter of a byte method is the name `byte_string_view`.

`this` is only the receiver.

```c
import "std/string";

func main() i32 {
    var text string.byte_string_view = "hello"b;
    if (text.equals("hello"b) && text.find_first_of("ol"b) == 2) {
        return text.find("lo"b);
    }
    return 0;
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $5, %ecx                    # constant
callq equals                     # call equals
retq                             # return

equals:
movq %rdi, -40(%rsp)             # move
movl %esi, -32(%rsp)             # move
movl %ecx, -16(%rsp)             # move
movq %rdx, -24(%rsp)             # move
movl -32(%rsp), %eax             # move
movl -16(%rsp), %ecx             # move
movq -24(%rsp), %rdx             # move
cmpl %ecx, %eax                  # compare
je .L1                           # branch
xorl %eax, %eax                  # zero the result
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
movl $0, -4(%rsp)                # constant
.L7:
movl -4(%rsp), %eax              # move
movl -32(%rsp), %ecx             # move
movq -40(%rsp), %rdx             # move
cmpl %ecx, %eax                  # compare
jge .L3                          # branch
movq -40(%rsp), %rax             # move
movslq -4(%rsp), %rcx            # move
movzbl (%rax,%rcx), %eax         # move
movl -16(%rsp), %ecx             # move
movq -24(%rsp), %rcx             # move
movl -4(%rsp), %edx              # move
movslq %edx, %rdx                # move
movzbl (%rcx,%rdx), %ecx         # move
cmpl %ecx, %eax                  # compare
jne .L4                          # branch
jmp .L5                          # jump
.L3:
movb $1, %al                     # constant
retq                             # return
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
.L5:
jmp .L6                          # jump
.L6:
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
movl %eax, -4(%rsp)              # move
jmp .L7                          # jump
```

## What the methods return

The program returns 3.

`equals` is true when the lengths match and every byte matches.

`equals("", "")` is true.

`compare` walks unsigned bytes and returns `-1`, `0`, or `1`.

A shared prefix leaves the shorter view first, so `compare("", "a")` is negative and `compare("", "")` is `0`.

`find_first_of` returns the first index whose byte is in the set, or `-1`.

An empty view and an empty set are `-1`.

`find` returns the first index of the needle, or `-1`.

An empty needle returns `0`, including on an empty view.

`starts_with` and `ends_with` compare that range.

`if (text.equals("hello"))` is a `bool` condition.

`!text.equals("hello")` is the `bool` not of that result.

## A constant view

`const greeting string.string_view = "hello"` keeps the alias.

The header is the same slice as `const label = "hello"`.

[Index](../README.md) · [Previous: Strings](../types/string.md) · [Next: unicode](unicode.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
