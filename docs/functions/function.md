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

A body may omit the result. The type is taken from the `return` expressions, and every `return` has to agree. `func add(a i32, b i32) { return a + b; }` is `i32`. A local may be written before that `return`. One expression uses that expression's type. Several expressions in one `return` are several results when every `return` agrees. A prototype still writes the result, and a later body has to agree or the program is `redefinition`. A generic omits the result the same way: `func id[T type](value T) { return value; }`. A method body may omit it too. A body with no `return`, a bare `return;`, and `return` expressions whose types differ are `not supported yet`. `typeof` is not a result type. An anonymous function that omits the result is `void`.

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

[Index](../README.md) · [Previous: assert](../statements/assert.md) · [Next: Several results](result.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
