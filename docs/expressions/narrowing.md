# Narrowing

[Index](../README.md) · [Previous: Pointers and null](null.md) · [Next: Conditions](condition.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page describes how a `?*T` name becomes `*T` after a `null` check, and how postfix `!` asserts one use.

## Where a name becomes `*T`

A name compared with `null` is `*T` where that comparison shows it has a value.

After `if (p == null) { return; }`, `p` is `*T` for the rest of the function.

`if (p != null) { use(p); }` narrows `p` inside the block.

The `else` of `p == null`, and the `else` of `p != null` when that `else` always leaves, do the same.

`while (p != null)` and `for (; p != null;)` narrow the body.

> **Warning:** The body of `do` runs before the check and stays `?*T`.

A field comparison such as `person.home == null` does not narrow.

`*p`, `p.field`, and `p[i]` on a `?*T` are reported as `type mismatch` until the name is narrowed.

Passing `?*T` where `*T` is required is reported as `type mismatch` until then.

## Postfix `!`

Postfix `!` asserts a `?*T` at that use.

`pointer!` has type `*T`.

`myfunc(pointer!)` passes it where `*T` is required, and `pointer!.value` reads a field.

> **Warning:** The name stays `?*T` for the next use.

`!` on a value that is already `*T`, or on a non-pointer, is reported as `type mismatch`.

`&n` is `*i32`.

The compiler reports `var p ?*i32 = &n` as `type mismatch: expected ?*i32, found *i32`.

A null `p!` calls `builtin.panic("null pointer")` and does not return.

## Prefix `!`

Prefix `!` is logical not on a `bool` and yields `bool`.

The compiler reports an integer as `type mismatch: expected bool, found i32`.

```c
func read(p *i32) i32 {
    return *p;
}

func show(p ?*i32) i32 {
    return read(p!);
}

func main() i32 {
    var n i32 = 40;
    var p *i32 = &n;
    return read(p);
}
```

```asm
main:
# trace frame for the call, then the matching pop
callq read                       # call read
retq                             # return

read:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
retq                             # return
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

```asm
bind:
# trace frame for the call, then the matching pop
callq settle                     # call settle
retq                             # return

settle:
movq %rdi, -16(%rsp)             # move
movq %rsi, -8(%rsp)              # move
movq -16(%rsp), %rax             # move
movq -8(%rsp), %rcx              # move
movq %rcx, (%rax)                # move
retq                             # return
```

[Index](../README.md) · [Previous: Pointers and null](null.md) · [Next: Conditions](condition.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
