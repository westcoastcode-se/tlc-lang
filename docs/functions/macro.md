# Macros

[Index](../README.md) · [Previous: Comptime](comptime.md) · [Next: typeof is not a result](typeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A `macro func` generates statements. A statement call is replaced by the statements written with `emit`. A parameter without `comptime` is replaced by the argument expression. A `comptime` parameter is a value known while compiling. The values are the same ones a [`comptime func`](comptime.md) accepts. `emit` outside a `macro` is `not supported yet`.

```c
extern(abi="c") func printf(fmt *const u8, ...) i32;

macro func hello(name comptime []const u8, tail []const u8, n comptime i32) void {
    emit printf("Hello %s %s %d\n", name, tail, n);
}

const label = "Per";

func main() i32 {
    var tail = "Ada";
    hello(label, tail, 40 + 2);
    return 0;
}
```

`hello(label, tail, 40 + 2)` emits `printf("Hello %s %s %d\n", "Per", tail, 42)`. The file-scope `const` string and the folded integer are comptime values. `tail` has no `comptime` mark, so the argument expression is pasted and read when the statement runs. A `var` passed to a `comptime` parameter is `not supported yet`. Using one parameter in two `emit` statements evaluates that argument again.

```c
macro func put(slot *i32, value i32) void {
    emit *slot = value;
    emit *slot = *slot + value;
}
```

`put(&n, k + 2)` stores `k + 2`, then adds `k + 2` again.

In a `macro`, `if` keeps the arm whose condition folds. The other arm is discarded. `for` unrolls. There is no `comptime` prefix on either statement. `comptime if` and `comptime for` are `not supported yet`. `const for` is not a form.

```c
macro func pick(slot *i32, n comptime i32) void {
    if (n == 1) {
        emit *slot = 40;
    } else {
        emit *slot = 2;
    }
}

macro func add3(slot *i32) void {
    for (var i = 1; i < 4; i = i + 1) {
        emit *slot = *slot + i;
    }
}
```

`pick(&n, 1)` stores 40. `add3` emits the body for `1`, `2`, and `3`. `for (var i = low; i < high; i = i + step)` unrolls when `low`, `high`, and `step` fold to integers, `step` is positive, and the trip count is at most 32. The index in each copy is that integer. `for (var item : items)` unrolls when the length is known, at most 32 elements. A slice `var`, `ptr_of` on a temporary, a length past 32, a bound that does not fold, and a step that is not positive are `not supported yet`. Those loops are on [`for`](../statements/for.md). A condition that does not fold is `not supported yet`.

`emit for`, `emit if`, `emit while`, and `emit do` paste that statement into the caller. The body is ordinary code, so a nested `for` or `if` runs later. The bound of an `emit for` may be a `var`. Those forms outside a `macro` are `not supported yet`. A plain `while` or `do` in a `macro` is `not supported yet`.

A value call of a `macro`, `var f = name`, a method, `extern`, and `export` are `not supported yet`. A type parameter is accepted. The parameter is the argument expression, so a tuple may contain a `var`. `values comptime T` does not: a tuple that contains a `var` is `not supported yet`.

```c
import "builtin";

extern(abi="c") func printf(fmt *const u8, ...) i32;

macro func show[T type](values T) void {
    for (var i = 0; i < values.len; i = i + 1) {
        if (typeof(values[i]) == typeof(i32)) {
            emit printf("%d\n", values[i]);
        } else {
            emit printf("%s\n", values[i]);
        }
    }
}
```

`show(("John Doe", age))` emits `printf("%s\n", "John Doe")` and `printf("%d\n", age)` when `age` is a `var`. [`builtin.fmt`](../expressions/tuple.md) fills `{0}` and `{1}` from a compile-time tuple.

## Assembly

```asm
main:
# trace frame for the call, then the matching pop
callq printf                     # call printf
retq                             # return
```

[Index](../README.md) · [Previous: Comptime](comptime.md) · [Next: typeof is not a result](typeof.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
