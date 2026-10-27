# Functions

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes a function's parameters, its result type, and how a call selects it.

## The declaration

A function starts with `func`, then the name, then the parameters, and then the result type when it is written.

A parameter is a name followed by its type, and an empty parameter list means there are no parameters.

There is one `main`.

A [`comptime func`](comptime.md) calculates while compiling.

A [`macro func`](macro.md) pastes the statements written with `emit`.

## Pointer parameters

A parameter `*T` is assumed to store into that object, so the caller may pass storage that is not yet initialized. The object is initialized after the call.

A parameter `*const T` reads the object, so the storage has to be initialized first. The compiler reports `uninitialized` when that storage is left unset.

The same rule applies to a method receiver, `self *this` and `self *const this`.

## Array parameters

A parameter `[N]T` is a pointer to the first element, the same pointer as `*T`. Passing the array does not initialize its elements, and a later read is `uninitialized`.

A parameter `*[N]T` points at the array, so that call initializes it. `*const [N]T` reads the array, so the elements have to be initialized first.

> **Warning:** A sized array of `T` may be passed where `*T` or `*const T` is written. That argument is a pointer to the first element. The call does not initialize the elements, and `*const T` does not require them. A later read is `uninitialized`. `*[N]T` does not accept the array. The argument there is `&array`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func main() i32 {
    return add(20, 22);
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
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

## An omitted result

A body may omit the result. The type is taken from the `return` expressions, and every `return` has to agree.

`func add(a i32, b i32) { return a + b; }` is `i32`. A local may be written before that `return`, and one expression uses that expression's type.

Several expressions in one `return` are several results when every `return` agrees.

A prototype still writes the result, and a later body has to agree or the program is `redefinition`.

A generic omits the result the same way: `func id[T type](value T) { return value; }`. A method body may omit the result too.

The omitted result may be a function pointer, and that pointer may take `[]const u8`. Its result may be a function pointer.

`func pick() { return add; }` is `func (i32, i32) i32` when `add` is that function. `func outer() { return id; }` is `func (i32) func (i32) i32` when `id` is that function.

A body with no `return` is `void`, and so is a body that only writes `return;`. An anonymous function that omits the result is that same `void`.

`func touch()` and `func touch() void` are that same function. `func done() { return; }` and `func done() void { return; }` are that same function.

The first `return` chooses the type. A later `return` of a different type is `return type: expected i32, found bool`.

A `return;` beside `return 1` is `return type: expected void, found i32` when the bare `return;` is first.

`func give() { return touch(); }` is `void` when `touch` is `void`, and `func give() void { return touch(); }` is `void` too.

An omitted result that is `Shape` or `*Shape` is `trait has no size`. `func id(s Shape) Shape` is that same error.

A `return` that calls a function whose result is not known yet does not choose the type.

`func pick(flag bool) { if (flag) { return 40; } return pick(true); }` is `i32`. The call is checked against that type.

`func f(something bool) { if (something) { return 1; } something = true; return f(something); }` is `i32`.

`func f() { return f(); }` is `infinite loop`, because every reached `return` calls `f`. `func a() { return b(); }` and `func b() { return a(); }` are `infinite loop` too.

`typeof` is not a result type.

```c
func add(a i32, b i32) {
    var n = a + b;
    return n;
}

func main() {
    return add(20, 22);
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
movl %edi, -12(%rsp)             # move
movl %esi, -8(%rsp)              # move
movl -12(%rsp), %eax             # move
addl -8(%rsp), %eax              # add
movl %eax, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

## A prototype and a later body

A function may be declared first and defined later, with the same type. The compiler reports `redefinition` for two bodies with the same parameter list.

```c
func add(a i32, b i32) i32;

func add(a i32, b i32) i32 {
    return a + b;
}
```

```asm
add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

## Several parameter lists

One name may have several parameter lists, and a call uses the types of the arguments.

`func show(n i32) i32` and `func show(text *const u8) i32` are both `show`.

The receiver is not one of those parameters. `func Point::draw(self *this)` and `func Circle::draw(self *this)` are two methods.

`func Point::draw(self this)` beside `func Point::draw(self *this)` stays `redefinition`.

`func Point::draw(self *this, n i32)` is another `draw` on `Point`, chosen from the remaining arguments.

The compiler reports `type mismatch` or `argument count` when a call matches none or more than one list.

Using the name as a value when several lists exist is `not supported yet`.

[`unsafe func`](../expressions/unsafe.md) makes the body an unsafe context. The caller writes `unsafe name(...)`.

```c
func show(n i32) i32 {
    return n;
}

func show(text *const u8) i32 {
    return 2;
}

func main() i32 {
    var text *const u8 = *const u8(0);
    return show(1) + show(text);
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $1, %edi                    # constant
callq i32                        # call i32
retq                             # return

*constu8:
movl $2, %eax                    # constant
retq                             # return

i32:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

The program returns 3.

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
