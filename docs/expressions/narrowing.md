# Narrowing

[Index](../README.md) · [Previous: Pointers and null](null.md) · [Next: Conditions](condition.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A name compared with `null` is `*T` where that comparison shows it has a value. After `if (p == null) { return; }`, `p` is `*T` for the rest of the function. `if (p != null) { use(p); }` narrows `p` inside the block. The `else` of `p == null`, and the `else` of `p != null` when that `else` always leaves, do the same. `while (p != null)` and `for (; p != null;)` narrow the body. The body of `do` runs before the check and stays `?*T`. A field comparison such as `person.home == null` does not narrow. `*p`, `p.field`, and `p[i]` on a `?*T` are reported as `type mismatch` until the name is narrowed. Passing `?*T` where `*T` is required is reported as `type mismatch` until then.

Postfix `!` asserts a `?*T` at that use. `pointer!` has type `*T`. `myfunc(pointer!)` passes it where `*T` is required, and `pointer!.value` reads a field. The name stays `?*T` for the next use. `!` on a value that is already `*T`, or on a non-pointer, is reported as `type mismatch`. The compiler does not insert a null check. Prefix `!` is logical not on a `bool` and yields `bool`. An integer is reported as `type mismatch: expected bool, found i32`.

```c
func read(p *i32) i32 {
    return *p;
}

func main() i32 {
    var n i32 = 40;
    var p ?*i32 = &n;
    return read(p!);
}
```

```c
type Address struct {
    street i32;
};

type Person struct {
    home *Address;
};

func settle(person *Person, home *Address) void {
    person.home = home;
}

func bind(person ?*Person, home ?*Address) void {
    if (person == null) {
        return;
    }
    if (home == null) {
        return;
    }
    settle(person, home);
}
```

## Cost

Narrowing a `?*T` to `*T` is a check the compiler remembers. It adds no instructions at the `if (p != null)`. The later `*p` is an ordinary load: reload the pointer, load the `i32`, 2 instructions.

`p!` is the same 2 instructions as `*p`. The postfix adds no compare and no branch. The function `return *p!` is 4 instructions including the pointer store and `retq`.

[Index](../README.md) · [Previous: Pointers and null](null.md) · [Next: Conditions](condition.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
