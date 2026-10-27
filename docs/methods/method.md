# Methods

[Index](../README.md) · [Previous: Function pointers](../functions/pointer.md) · [Next: Generic methods](generic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A method belongs to a struct, an enum, or a primitive type alias.

## Where the method is written

A primitive alias such as `type Integer i32` can have a method.

The method is written in the same file as that type.

A method in any other file is reported as `method in another file`.

## The receiver

The receiver is the first parameter.

It is the name, then `this`: `self this`.

`this` stands for that struct, enum, or alias.

A pointer receiver is `self *this`, the same order as `*Point`.

Assigning that name, `self = other`, is reported as `type mismatch`.

`*self = other` stores through the pointer.

`self.x = 1` stores a field.

A value receiver `self = other` stores that value.

`self **this` is reported as `expected 'this'`.

`self ?*this` may be `null`.

A `*T` value is not passed to it.

`this` is an ordinary identifier in every other position.

On an enum, the receiver is that enum's value.

On `type Integer i32`, the receiver is an `Integer`, not an `i32`.

A function with no receiver is a static function.

The builtin name `i32` cannot have a method.

A pointer alias, a sized-array alias, and a struct alias cannot either.

A static function on those aliases is `Ptr.from(...)`, described in [Static functions](static.md).

A slice alias such as `type string_view []const u8` can.

The receiver is the value `self this`.

`self *this` on a slice alias is a pointer to that slice.

The functions on that alias live in [`std/string`](../std/string.md), which is `lib/tlc/std/string`.

The call omits the receiver.

The program below returns 42.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::sum(self this, extra i32) i32 {
    return self.x + self.y + extra;
}

func Point::add(self *this, dx i32, dy i32) void {
    self.x = self.x + dx;
    self.y = self.y + dy;
}

func main() i32 {
    var p Point;
    p.x = 10;
    p.y = 10;
    p.add(1, 1);
    return p.sum(20);
}
```

```asm
add:
movq %rdi, -16(%rsp)             # move
movl %esi, -8(%rsp)              # move
movl %edx, -4(%rsp)              # move
movq -16(%rsp), %rax             # move
movq -16(%rsp), %rcx             # move
movl (%rcx), %ecx                # move
addl -8(%rsp), %ecx              # add
movl %ecx, (%rax)                # move
movq -16(%rsp), %rax             # move
movq -16(%rsp), %rcx             # move
movl 4(%rcx), %ecx               # move
addl -4(%rsp), %ecx              # add
movl %ecx, 4(%rax)               # move
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $1, %edx                    # constant
movl %edx, %esi                  # move
callq add                        # call add
retq                             # return
```

## An omitted result

A method body may omit the result the same way a function does, including when that result is a function pointer.

A body with no `return` is `void`, and so is a body that only writes `return;` or returns a `void` value.

That pointer may take `[]const u8`.

That pointer's result may be a function pointer.

The type comes from the `return` expressions.

`add` takes a pointer, so `p` must be an lvalue.

The compiler passes `&p`.

`sum` takes a value.

A call on a `*Point` loads the struct.

A pointer method on a temporary struct value is reported as `not an lvalue`.

## Const receivers

A const variable, and a `*const Point`, cannot call a method whose receiver is a non-const pointer.

`origin.bump()` and `frozen.bump()`, when `bump` is `self *this`, are reported as `type mismatch: expected *Point, found *const Point`.

The same call through `?*this` is reported the same way, with `?*Point` as the expected type.

`self *const this` and a value receiver can be called on that variable.

A store through `self *const this` is reported as `type mismatch`.

A const binding of a mutable pointer still calls `self *this`: `const p = &live` then `p.bump()` changes `live`.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::read(self *const this) i32 {
    return self.x + self.y;
}

func Point::value(self this) i32 {
    return self.x;
}

func Point::bump(self *this) void {
    self.x = self.x + 1;
}

const origin Point = {.x = 20, .y = 2};

func main() i32 {
    var live Point;
    live.x = 9;
    const p = &live;
    p.bump();
    const frozen = live;
    return origin.read() + frozen.read() + frozen.value();
}
```

This example is rejected (`uninitialized`) and emits nothing.

The program returns 42.

## An optional pointer

`self ?*this` may be `null`.

`p.read()` and `var q ?*Point = &p` are `type mismatch: expected ?*Point, found *Point`.

```c
type Point struct {
    x i32;
    y i32;
};

func Point::read(self ?*this) i32 {
    if (self == null) {
        return 0;
    }
    return self.x;
}

func main() i32 {
    var q ?*Point = null;
    return q.read();
}
```

```asm
read:
movq %rdi, -8(%rsp)              # move
cmpq $0, -8(%rsp)                # compare
jne .L1                          # branch
xorl %eax, %eax                  # zero the result
retq                             # return
.L1:
jmp .L2                          # jump
.L2:
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
retq                             # return
```

The program returns 0.

`q.read()` passes the optional pointer.

## The same name in more than one place

The same method name may exist on different structs and as a free function.

The call `p.sum(...)` selects the struct's method.

A free call `sum(...)` selects the function.

## Calls from another file

Every file in the package can call the method.

`export func Point::sum` publishes that method.

Another package calls `value.sum(...)`.

A method without `export` is reported as `sum is not exported`.

`export func Vec[T]::get` publishes that instance method.

Another package calls `value.get(...)`.

`export` on a static function publishes `pkg.Type.name(...)`, including `pkg.Vec[i32].name(...)`.

`Point::sum(p, 1)` is not a call.

`pkg.Point.sum` is reported as `not supported yet`.

## extern

### A method in a tlc library

`extern func Point::read` names that method in a tlc library.

The call is still `p.read()`.

A function with no receiver is still `Point.origin()`.

The symbol is `qualifier.Point.read`, the same spelling an exported method uses.

A body defines that symbol.

`extern(abi="c")` keeps the name `Point.read` and the C calling convention.

A struct in that signature, including the receiver, must be `extern(abi="c")`, or the error is `struct must be extern(abi="c")`.

`extern(abi="c", name="symbol")` sets the C object symbol and leaves the call unchanged.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

Two different names on one parameter list are `redefinition`.

Two `extern(abi="c")` parameter lists on one method are `redefinition`.

### A generic struct

`extern func Vec[T]::get` is the same rule on a generic struct.

Each specialization is its own symbol.

The call stays `value.get(...)`.

`extern(abi="c")` and `extern(abi="c", name="symbol")` on that method are `extern(abi="c") on a trait or a generic`.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

### A trait default

`extern func Shape::twice` names a trait default.

The default is copied onto each struct that gains it.

Each copy is its own symbol, the same spelling an exported method on that struct uses.

The call stays `s.twice()` or `r.twice()`.

`extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are `extern(abi="c") on a trait or a generic`.

`extern(name="symbol")` is `name requires abi="c"`.

A prototype with no body is `not supported yet`.

### A method template

`extern func Point::id[T type]` names a method template.

The call stays `p.id[i32](value)`.

Each specialization is its own symbol, such as `qualifier.Point.id.spec.i32`.

A body defines that symbol.

`extern(abi="c")` on that template is `extern(abi="c") on a trait or a generic`.

`extern(name="symbol")` is `name requires abi="c"`.

A prototype with no body is `not supported yet`.

### A trait parameter

`extern func Rect::paint(self *this, s Shape)` names a method whose parameter is a trait.

The call stays `value.paint(s)`.

Each struct is its own symbol, such as `qualifier.Rect.paint.spec.Circle`.

A body defines that symbol.

A prototype may say `extern`.

The body that follows defines it.

A call of a prototype with no body is `not supported yet`.

`...` on that method is `not supported yet`.

```c
type Point struct {
    x i32;
    y i32;
};

extern func Point::read(self this) i32 {
    return self.x + self.y;
}

extern func Point::origin() Point {
    return Point{.x = 20, .y = 22};
}

func main() i32 {
    return Point.origin().read();
}
```

```asm
origin:
movl $20, -8(%rsp)               # constant
movl $22, -4(%rsp)               # constant
movl -8(%rsp), %eax              # move
movl -4(%rsp), %edx              # move
retq                             # return

read:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

The program returns 42.

[Index](../README.md) · [Previous: Function pointers](../functions/pointer.md) · [Next: Generic methods](generic.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
