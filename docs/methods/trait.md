# Traits

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A trait is a list of methods and fields.

## Writing the trait

The receiver is implicit and is omitted from the trait type.

A method inside the braces ends with `;`.

`func area() i32 { return 1; }` is reported as `expected ';'`.

A field is a name and a type, and it ends with `;`.

A value on the field, `w i32 = 1`, is reported as `expected ';'`.

`extern(abi="c") type Shape trait` is `extern(abi="c") on a trait or a generic`.

`export type Shape trait` publishes that trait.

It has no symbol.

Another package writes `T pkg.Shape`.

`extern type Shape trait` is `not supported yet`.

```c
type Shape trait {
    func area() i32;
    func grow(n i32) void;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func Rect::grow(self *this, n i32) void {
    self.w = self.w + n;
    self.h = self.h + n;
}

func measure(s *Shape) i32 {
    return s.area();
}

func apply(s *Shape, n i32) i32 {
    s.grow(n);
    return measure(s);
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return apply(&r, 1);
}
```

```asm
main:
pushq %rax
movl $5, (%rsp)                  # constant
movl $6, 4(%rsp)                 # constant
movq %rsp, %rdi                  # move
movl $1, %esi                    # constant
callq apply                      # call apply
popq %rcx
retq                             # return

grow:
movq %rdi, -16(%rsp)             # move
movl %esi, -4(%rsp)              # move
movq -16(%rsp), %rax             # move
movq -16(%rsp), %rcx             # move
movl (%rcx), %ecx                # move
addl -4(%rsp), %ecx              # add
movl %ecx, (%rax)                # move
movq -16(%rsp), %rax             # move
movq -16(%rsp), %rcx             # move
movl 4(%rcx), %ecx               # move
addl -4(%rsp), %ecx              # add
movl %ecx, 4(%rax)               # move
retq                             # return

apply:
# trace frame for the call, then the matching pop
callq grow                       # call grow
retq                             # return
```

`apply(&r, 1)` changes the width and height to 6 and 7.

`area` returns 42.

## A parent trait

`type Shape trait(Parent)` copies `Parent`'s methods and fields into `Shape` while compiling.

The child still stores `Parent` as its parent.

A copied field or method keeps that trait as the place it was declared.

`func need(p Parent)` accepts `s` when `s` is `Shape`.

The call is still the struct's method.

There is no table.

A struct passed as `Shape` must provide those methods and fields too.

Writing the same method or field again is `redefinition`.

A missing parent is `undeclared identifier`.

A parent that is not a trait, or a cycle, is `type mismatch`.

`trait(pkg.Parent)` is `not supported yet`.

## How a struct satisfies a trait

There is no `impl` keyword.

A struct satisfies a trait by defining the methods.

Those methods use the same result type and the same parameters after the receiver.

The struct also has each field with the same type.

The check happens where a concrete struct value or a concrete pointer is passed to a trait parameter.

- `s Shape` requires every method to have a value receiver.
- `s *Shape` accepts both value receivers and pointer receivers.
- A pointer method passed as a value is reported as `type mismatch`.
- A missing method is reported as `undeclared identifier`.
- A missing field is reported as `undeclared identifier`.
- A wrong result type, wrong extra parameters, or a field of another type are reported as `type mismatch`.
- A field named twice, or a field and a method of the same name, is reported as `redefinition`.

> **Warning:** The address is not taken automatically. `apply(r)` is reported as `type mismatch` when the parameter is `*Shape`. The call is `apply(&r)`.

### Unsafe methods

`unsafe func draw() void;` makes that method unsafe.

The call is `unsafe s.draw()` and is reported as `unsafe call` without the prefix.

The struct method is `unsafe func Rect::draw`.

Leaving `unsafe` off that method, or writing it when the trait method has none, is reported as `type mismatch`.

An anonymous function passed to that trait is not an `unsafe func`.

The call through the trait still writes `unsafe`.

## A constraint on a type parameter

A generic function writes the same constraint as `T Shape`.

`T pkg.Shape` names an exported trait in that package.

`value *T` follows `*Shape`, and `value T` follows `Shape`.

See [Generics](../functions/generic.md).

## Where a trait value cannot be stored

`trait` is written only in `type Shape trait`.

A parameter is `Shape` or `*Shape`.

`var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, and `[]Shape` are `trait has no size`.

An omitted result is that same error: `func id(s Shape) { return s; }` and `func id(p *Shape) { return p; }`.

That includes `[2]*Shape`, `[]*Shape`, a parameter `[]Shape`, and a field `item Shape` or `item *Shape` in a struct or a trait.

`var s trait Shape`, `sizeof(trait Shape)`, and `alignof(trait Shape)` are reported as `expected a type`.

`sizeof(Shape)` and `alignof(Shape)` are reported as `undeclared identifier`.

## One copy for each struct

The function is monomorphized.

Each struct that is passed in gets its own copy.

The call `s.area()` becomes a direct call of that struct's method.

`type Shape trait(Parent)` adds no instruction and no table.

`main` that stores 5 and 6 and returns `use(r)`, where `area` is copied from `Parent`, executes 8 instructions.

The call is `main.use.spec.Rect`, and `s.area()` inside it is a direct call of `Rect.area`.

Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

## Several results

A trait method may return several results, the same way a function does.

`var a, b = p.both()` unpacks them.

One name binds the tuple.

A different count is reported as `type mismatch`.

The struct method returns that same list.

A default is `func Pair::both` outside the braces.

## Fields

A trait names a field that the struct already has.

`s.w` reads it, and `s.w = 1` writes it, for both `s Shape` and `s *Shape`.

Writing through `*const Shape` is reported as `type mismatch`.

## Choosing one parameter list

`Drawable(value)` names that trait when one function or one method has several parameter lists and those lists take different traits.

`use(Drawable(s))` and `p.show(Movable(s))` select the list.

A child trait is one of those lists: `use(Named(r))` and `use(Shape(r))` when `Named` is `trait(Shape)`.

The value has to satisfy the trait that was written.

`use(s)` and `p.show(s)` are `type mismatch`, including when the value satisfies only one list.

A generic trait takes its type from the value.

`Getter(h)` is `Getter[i32]` when `h.get()` returns `i32`, and `seq.Seq(items)` is `seq.Seq[i32]` when `items` is an array of `i32`.

`Getter[i32](h)` and `seq.Seq[i32](items)` write that type.

`var d Drawable = Drawable(s)` is `trait has no size`.

> **Warning:** `use(*Drawable(&s))` is a pointer cast. `--check` accepts it and `--run` reports `type mismatch`.

The cast adds no instruction.

`main` that stores 4 and returns `use(Drawable(s))` executes 6 instructions, the same as `use(s)`.

Those instructions are the store, the copy into the argument register, and `callq`, with no trace frame at that call.

One list keeps the symbol `main.use.spec.Sprite`.

Several lists put the trait in the name, such as `main.take.spec.Named.Rect`.

```c
type Shape trait {
    w i32;
    h i32;
    func area() i32;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

func widen(s *Shape) i32 {
    s.w = s.w + 1;
    return s.area();
}

func edges(s Shape) i32 {
    return s.w + s.h;
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return widen(&r) + edges(r) - 6;
}
```

```asm
main:
subq $24, %rsp                   # subtract
movl $5, 16(%rsp)                # constant
movl $6, 20(%rsp)                # constant
leaq 16(%rsp), %rdi              # address
callq widen                      # call widen
movl %eax, 12(%rsp)              # move
movl 16(%rsp), %edi              # move
movl 20(%rsp), %esi              # move
callq edges                      # call edges
movl %eax, %ecx                  # move
movl 12(%rsp), %eax              # move
addl %ecx, %eax                  # add
subl $6, %eax                    # subtract
addq $24, %rsp                   # add
retq                             # return

area:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
imull -4(%rsp), %eax             # multiply
retq                             # return

widen:
# trace frame for the call, then the matching pop
callq area                       # call area
retq                             # return

edges:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

`widen` adds 1 to `w` and `area` returns 36.

`edges` reads the same fields and returns 12.

The program returns 42.

## A function as the trait value

A trait with one method and no fields also accepts an anonymous function, and a named function with that method's parameters and result.

There is no receiver.

The short form and the `func` form both work.

The parameter may be the trait or a pointer to the trait.

The function is passed directly.

`pkg.inc` is that named function when `inc` is exported.

`Type.name` is that named function when it is a static function with the method's signature.

A function written inside another function is the same argument when it does not read a local or a parameter from the enclosing function.

A trait with a field, with no methods, or with several methods, is reported as `type mismatch`.

The wrong parameters or the wrong result are reported as `type mismatch`.

An overloaded name is reported as `not supported yet`.

A prototype with no body is reported as `type mismatch`.

`var f = inc` followed by passing `f` is reported as `type mismatch`.

An `unsafe func` used as the argument is reported as `type mismatch`.

An inner function that reads an enclosing local is reported as `not supported yet`.

```c
type Adder trait {
    func apply(n i32) i32;
};

func twice(a Adder, n i32) i32 {
    return a.apply(n) + a.apply(n);
}

func inc(n i32) i32 {
    return n + 1;
}

func main() i32 {
    return twice((n) { return n + 1; }, 20) + twice(inc, 3);
}
```

```asm
main:
pushq %rax
leaq 72(%rip), %rdi              # address
movl $20, %esi                   # constant
callq fn                         # call fn
movl %eax, 4(%rsp)               # move
leaq 35(%rip), %rdi              # address
movl $3, %esi                    # constant
callq fn                         # call fn
movl %eax, %ecx                  # move
movl 4(%rsp), %eax               # move
addl %ecx, %eax                  # add
popq %rcx
retq                             # return

inc:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return

1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return

fn:
# trace frame for the call, then the matching pop
callq *%rax                      # call *%rax
retq                             # return
```

`twice` calls that function twice.

The `func` form is the same argument, and so is `inc`.

`twice(inc, 3)` returns 8.

A method takes the function the same way: `c.add(inc)`.

`*Adder` takes it directly, as `take_ptr(inc)`.

`(*a).apply(n)` is that same call when `a` is `*Adder`.

One monomorphized copy calls it through a function pointer.

A struct argument still gets its own copy and a direct call, including `(*a).apply(n)`.

Passing the parameter onward, when the next parameter is the same trait, uses that same copy.

```c
type Adder trait {
    func apply(n i32) i32;
};

type Counter struct {
    base i32;
};

func through(a *Adder, n i32) i32 {
    return (*a).apply(n);
}

func Counter::add(self *this, a Adder) i32 {
    return self.base + a.apply(1);
}

func Counter::forward(a *Adder, n i32) i32 {
    return through(a, n);
}

func main() i32 {
    var c Counter;
    c.base = 6;
    var from_method = c.add(func (n i32) i32 { return n; });
    var from_static = Counter.forward((n) { return n + 1; }, 4);
    return from_method + from_static;
}
```

```asm
1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return

fn:
# trace frame for the call, then the matching pop
movl $1, %edi                    # constant
callq *%rax                      # call *%rax
retq                             # return

2:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $1, %eax                    # add
retq                             # return

fn:
subq $24, %rsp                   # subtract
movq %rdi, 8(%rsp)               # move
movl %esi, 20(%rsp)              # move
movq 8(%rsp), %rdi               # move
movl 20(%rsp), %esi              # move
callq fn                         # call fn
addq $24, %rsp                   # add
retq                             # return

fn:
# trace frame for the call, then the matching pop
callq *%rax                      # call *%rax
retq                             # return
```

`c.add` passes the `func` form to a method.

`Counter.forward` passes the short form to a static function whose parameter is `*Adder`, and `through` receives that same parameter.

## Names that are not values

`this` inside a trait method is reported as `type mismatch`.

`s.area` without a call is reported as `undeclared identifier`.

## Default methods

A default method is a function written on the trait, with a body, outside the braces.

`extern func Shape::twice` names that default in a tlc library.

The default is copied onto each struct that gains it.

Each copy is its own symbol, the same spelling an exported method on that struct uses, so the copy on `Rect` is `qualifier.Rect.twice`.

A body defines that symbol.

`extern(abi="tlc")` is the same declaration.

`extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are `extern(abi="c") on a trait or a generic`.

A C symbol is one concrete library name.

The default is copied onto each struct.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

A prototype with no body is `not supported yet`.

`s.twice()` when `s` is `*Shape` runs the body.

`r.twice()` when `r` is `Rect` runs the copy `Rect` gained.

The struct's own method with the same parameters after the receiver is a method of that struct.

A call on the struct uses that body.

The call through the trait uses it too.

A different parameter list is another method.

A missing method that the trait requires, and that no default supplies, is reported as `undeclared identifier`.

A default whose parameters, result, and `unsafe` match a required method supplies that method when the struct omits it.

That call is still written on the trait, or on the struct that gained the copy.

```c
type Shape trait {
    func area() i32;
};

func Shape::twice(self *this) i32 {
    return self.area() + self.area();
}

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self *this) i32 {
    return self.w * self.h;
}

func use(s *Shape) i32 {
    return s.twice();
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return use(&r);
}
```

```asm
main:
pushq %rax
movl $5, (%rsp)                  # constant
movl $6, 4(%rsp)                 # constant
movq %rsp, %rdi                  # move
callq use                        # call use
popq %rcx
retq                             # return

twice:
# trace frame for the call, then the matching pop
callq area                       # call area
retq                             # return

area:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
movq -8(%rsp), %rcx              # move
imull 4(%rcx), %eax              # multiply
retq                             # return

use:
# trace frame for the call, then the matching pop
callq twice                      # call twice
retq                             # return
```

`use(&r)` is 60.

`a.malloc[i32]()` when `a` is `*Allocator` is the same default as `arena.malloc[i32]()`.

The call is specialized for the struct that was passed.

A struct method `Arena::malloc[T type]` wins over that default.

The result is `?*T`, and the caller handles `null`.

The call is `unsafe`.

## Export and extern

`export func use(s Shape)` publishes that function.

Another package calls `pkg.use(r)`, or `use(r)` after `import { use }`.

`export func Rect::paint(self *this, s Shape)` publishes that method.

Another package calls `value.paint(s)`.

The parameter is `Shape` or `*Shape`.

The call is still specialized.

The symbol of `use(r)` when `use` is in `main` is `main.use.spec.Rect`.

When the struct is in another package, its package is part of that symbol.

The struct's method is exported when the function and the struct are in different packages.

Without that `export`, the call is `area is not exported`.

A method in the same package does not need `export`.

`extern` and `extern(abi="tlc")` name that function in a tlc library.

Each struct is its own symbol.

The call stays `use(r)` or `value.paint(s)`.

A body defines that symbol.

A prototype may be the declaration that says `extern`, and the body that follows defines it.

A prototype with no body is accepted.

A call that specializes it is `not supported yet`.

`extern(abi="c")` and `extern(abi="c", name="symbol")` on that function are `extern(abi="c") on a trait or a generic`.

`...` is `not supported yet`.

`extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`.

```c
type Shape trait {
    func area() i32;
};

type Rect struct {
    w i32;
    h i32;
};

func Rect::area(self this) i32 {
    return self.w * self.h;
}

export func use(s Shape) i32 {
    return s.area();
}

func main() i32 {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return use(r);
}
```

```asm
main:
pushq %rax
movl $5, (%rsp)                  # constant
movl $6, 4(%rsp)                 # constant
movl (%rsp), %edi                # move
movl 4(%rsp), %esi               # move
callq use                        # call use
popq %rcx
retq                             # return

area:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
imull -4(%rsp), %eax             # multiply
retq                             # return

use:
# trace frame for the call, then the matching pop
callq area                       # call area
retq                             # return
```

`use(r)` returns 30.

`Rect::area` needs no `export` here, because `use` and `Rect` are in the same package.

## A generic trait

`type Seq[T type] trait` names one type parameter.

A parameter the trait does not use is `unused parameter`.

`Seq[i32]` and `Seq[u8]` are different traits.

A method may use `T` as a parameter or a result, and a field may use `T` as well.

A struct satisfies `Seq[i32]` by defining those methods and fields with `i32` in place of `T`.

There is no `impl`.

The parameter is `items Seq[i32]` or `items *Seq[i32]`.

`Seq` without arguments is reported as `type mismatch`.

An integer parameter, a pack, several parameters, and `trait(Parent)` on that list are reported as `not supported yet`.

`func Seq::at` is reported as `not supported yet`.

`export` publishes the trait.

Another package names `pkg.Seq[i32]`.

`extern` and `extern(abi="tlc")` are accepted.

The trait has no size and no symbol.

`extern(abi="c")` is reported as `extern(abi="c") on a trait or a generic`.

`func use[T type](items Seq[T])` is reported as `not supported yet`.

```c
type Seq[T type] trait {
    func at(i i32) T;
};

type Bag struct {
    n i32;
};

func Bag::at(self this, i i32) i32 {
    return self.n + i;
}

func use(items Seq[i32]) i32 {
    return items.at(2);
}

func main() i32 {
    var bag Bag;
    bag.n = 40;
    return use(bag);
}
```

```asm
main:
pushq %rax
movl $40, (%rsp)                 # constant
movl (%rsp), %edi                # move
callq use                        # call use
popq %rcx
retq                             # return

at:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

use:
# trace frame for the call, then the matching pop
movl $2, %esi                    # constant
callq at                         # call at
retq                             # return
```

`use(bag)` returns 42.

`at` is a direct call of `Bag.at`.

Passing a struct whose `at` returns `u8` where `Seq[i32]` is expected is reported as `type mismatch`.

`import "std/seq"` names that trait as the iteration trait.

`seq.Seq[T]` and `*seq.Seq[T]` check `len` and `at`.

A pointer to an element is `ptr_of`.

An array `[N]E` and a slice `[]E` satisfy `seq.Seq[E]`.

On those values `len` is `.len` and `at` is the index.

A struct still satisfies the trait by its own methods.

The declaration is on [seq](../std/seq.md).

[Index](../README.md) · [Previous: Constants on a type](constant.md) · [Next: trace](../builtin/trace.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
