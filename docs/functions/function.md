# Functions

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A function starts with `func`, then the name, then the parameters, and then the result type when it is written. A parameter is a name followed by its type. An empty parameter list means there are no parameters. There is one `main`. A parameter `*T` is assumed to store into that object, so the caller may pass storage that is not yet initialized, and the object is initialized after the call. A parameter `*const T` reads the object, so the storage has to be initialized first. Leaving it unset is reported as `uninitialized`. The same rule applies to a method receiver, `self *this` and `self *const this`. A parameter `[N]T` is a pointer to the first element, the same pointer as `*T`. Passing the array does not initialize its elements, and a later read is `uninitialized`. A parameter `*[N]T` points at the array, so that call initializes it. `*const [N]T` reads the array, so the elements have to be initialized first. A sized array of `T` may be passed where `*T` or `*const T` is written. That argument is a pointer to the first element. The call does not initialize the elements, and `*const T` does not require them. A later read is `uninitialized`. `*[N]T` does not accept the array. The argument there is `&array`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func main() i32 {
    return add(20, 22);
}
```

A body may omit the result. The type is taken from the `return` expressions, and every `return` has to agree. `func add(a i32, b i32) { return a + b; }` is `i32`. A local may be written before that `return`. One expression uses that expression's type. Several expressions in one `return` are several results when every `return` agrees. A prototype still writes the result, and a later body has to agree or the program is `redefinition`. A generic omits the result the same way: `func id[T type](value T) { return value; }`. A method body may omit it too. The omitted result may be a function pointer: `func pick() { return add; }` is `func (i32, i32) i32` when `add` is that function. That pointer may take `[]const u8`. Its result may be a function pointer: `func outer() { return id; }` is `func (i32) func (i32) i32` when `id` is that function. A body with no `return` is `void`, and so is a body that only writes `return;`. An anonymous function that omits the result is that same `void`. `func touch()` and `func touch() void` are that same function. `func done() { return; }` and `func done() void { return; }` are that same function. The first `return` chooses the type. A later `return` of a different type is `return type: expected i32, found bool`. A `return;` beside `return 1` is `return type: expected void, found i32` when the bare `return;` is first. `func give() { return touch(); }` is `void` when `touch` is `void`, and so is `func give() void { return touch(); }`. An omitted result that is `Shape` or `*Shape` is `trait has no size`, the same as `func id(s Shape) Shape`. A `return` that calls a function whose result is not known yet does not choose the type. `func pick(flag bool) { if (flag) { return 40; } return pick(true); }` is `i32`, and the call is checked against that type. `func f(something bool) { if (something) { return 1; } something = true; return f(something); }` is `i32`. `func f() { return f(); }` is `infinite loop`: every reached `return` calls `f`. `typeof` is not a result type.

```c
func add(a i32, b i32) {
    var n = a + b;
    return n;
}

func main() {
    return add(20, 22);
}
```

A function may be declared first and defined later, with the same type. Two bodies for the same parameter list are reported as `redefinition`. One name may have several parameter lists. A call uses the types of the arguments. `func show(n i32) i32` and `func show(text *const u8) i32` are both `show`. The receiver is not one of those parameters. `func Point::draw(self *this)` and `func Circle::draw(self *this)` are two methods. `func Point::draw(self this)` beside `func Point::draw(self *this)` stays `redefinition`. `func Point::draw(self *this, n i32)` is another `draw` on `Point`, chosen from the remaining arguments. A call that matches none or more than one list is `type mismatch` or `argument count`. Using the name as a value when several lists exist is `not supported yet`. [`unsafe func`](../expressions/unsafe.md) makes the body an unsafe context. The caller writes `unsafe name(...)`.

```c
func add(a i32, b i32) i32;

func add(a i32, b i32) i32 {
    return a + b;
}
```

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

The program returns 3.

## Cost

A function stores each parameter on entry. An `i32` parameter is 1 `movl`. A pointer is 1 `movq`. A float is 1 `movss` or `movsd`. `return a + b` for two `i32` parameters is 5 instructions: the two stores, the load, the add, and `retq`. Omitting the result type adds no instructions. `return a + b` from `func add(a i32, b i32)` is those same 5.

A call of that function is separate. `return add(20, 22)` executes 38 instructions in the caller when fewer than 32 frames are stored: the 36 of a no-argument traced call, plus 2 `movl` for the arguments. `add` then runs its own 5. A prototype with no body adds no instructions until a call. Choosing a parameter list adds no instructions. The call runs the selected function.

`func touch() { var n i32 = 1; }` is `void`. The local is never read, so that function is 1 instruction, `retq`. `func touch() void` with the same body is those same instructions. `touch()` followed by `return 42` in `main` is 34 instructions in `main` when fewer than 32 frames are stored. The callee's `retq` is extra. Omitting `void` adds no instructions.

`func done() { return; }` is `void` too. That function is 1 instruction, `retq`. `func done() void { return; }` is that same instruction. `done()` followed by `return 42` in `main` is those same 34 instructions. A `return;` adds no instructions.

`func give() { return touch(); }` is `void` when `touch` is `void`. `give` runs 33 instructions when fewer than 32 frames are stored: the traced call of `touch`, then `retq`. `touch` is 1 instruction, `retq`, and that call is extra. `func give() void { return touch(); }` is those same instructions. `give()` followed by `return 42` in `main` is 34 instructions in `main`. Omitting `void` adds no instructions.

`func pick(flag bool) { if (flag) { return 40; } return pick(true); }` is `i32`. `return pick(false)` in `main` is 37 instructions in `main` when fewer than 32 frames are stored. `pick(false)` runs 46 instructions, including the call. `pick(true)` runs 12: the test, `movl $40`, and `retq`. `func pick(flag bool) i32` with the same body is those same instructions. Omitting the result adds no instructions.

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
