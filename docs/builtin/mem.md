# memeql and memcmp

[Index](../README.md) · [Previous: panic](panic.md) · [Next: builtin/env/consts](family.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

`import "builtin"` loads the compiler package. `builtin.memeql` and `builtin.memcmp` compare sequences of one element type. The bodies are ordinary tlc in `lib/tlc/builtin/builtin.tlc`. Each slice form has a second overload whose parameters are `comptime`. That spelling is on [Comptime](../functions/comptime.md). That overload is chosen when both arguments are comptime values, and a call of two constant `[]const u8` values is folded while compiling. A `var` or a local `const` chooses the overload without `comptime`, and that call runs the body.

```c
import "builtin";

func main() i32 {
    assert(builtin.memeql[u8]("hi", "hi"), "eq");
    const order = builtin.memcmp[u8]("ab", "ac");
    return order;
}
```

Each name has two forms. `T type` is the element type. The lengths in the pointer form are element counts.

```c
export func memeql[T type](lhs comptime []const T, rhs comptime []const T) bool;
export func memeql[T type](lhs []const T, rhs []const T) bool;
export func memeql[T type](lhs *const T, lhslen i32, rhs *const T, rhslen i32) i32;
export func memcmp[T type](lhs comptime []const T, rhs comptime []const T) i32;
export func memcmp[T type](lhs []const T, rhs []const T) i32;
export func memcmp[T type](lhs *const T, lhslen i32, rhs *const T, rhslen i32) i32;
```

The slice form of `memeql` is true when the lengths match and every element matches. The pointer form returns `1` in that case and `0` otherwise. `memcmp` returns `-1` when the left side is less, `1` when it is greater, and `0` when the sequences match. The order is the order of `T`, using `<` on each element. A shared prefix takes the shorter length as less. A `u8` element is compared as the integer `0` through `255`. An `i32` element is compared as that number, which is a different order from the bytes of a C `memcmp`.

`builtin.memeql[u8]("hi", "hi")` is true. `builtin.memeql[u8]("hi", "ha")` and `builtin.memeql[u8]("hi", "hi!")` are false. `builtin.memeql[u8]("", "")` is true. `builtin.memeql[u8]("hello"[0:2], "he")` is true, and so is `builtin.memeql[u8](label, label)` when `label` is a file-scope `const` string in the same file. A null in the middle is a byte. `builtin.memeql[u8]("a\000b", "a\000b")` is true, and `builtin.memeql[u8]("a\000b", "ab")` is false. The terminating null is not a byte of the slice. Omitting `[u8]` infers `u8` when both arguments are constant strings. `builtin.memcmp[u8]("ab", "ab")` is `0`. `builtin.memcmp[u8]("ab", "ac")` is `-1`, and `builtin.memcmp[u8]("ac", "ab")` is `1`. `builtin.memcmp[u8]("a", "ab")` is `-1`, and `builtin.memcmp[u8]("ab", "a")` is `1`. `builtin.memcmp[u8]("", "")` is `0`.

`assert(builtin.memeql[u8]("hi", "hi"), "eq")` folds and adds no code. A false comparison fails the assert, and the diagnostic is the message. `const same = builtin.memeql[u8]("hi", "hi")` is the integer `1`. `const diff = builtin.memeql[u8]("hi", "ha")` is `0`. `var live = builtin.memeql[u8]("hi", "hi")` is a `bool`. `const order = builtin.memcmp[u8]("ab", "ac")` is the integer `-1`. `var live_cmp = builtin.memcmp[u8]("ab", "ac")` is an `i32`.

The pointer form runs the body, including when both pointers are string literals and both lengths are constant integers. `builtin.memeql[u8]("", 0, "", 0)` is `1`. `builtin.memcmp[u8]("a", 1, "ab", 2)` is `-1`. A length past the slice reads those addresses. A string literal stores a null after its bytes, and that null is an element when the length counts it.

A `var` or a local `const` runs the body. `builtin.memeql(text, "hi")` infers `T` when `text` is `[]const u8`. `assert` still requires a constant, so `assert` of that call is `not supported yet`. A bound past the string is not a constant slice. `const n = builtin.memeql[u8]("hello"[1:9], "ell")` is `global initializer is not a constant`. `builtin.memeql[i32]("hi", "hi")`, `builtin.memeql[bool]("hi", "hi")`, and `builtin.memeql[u8](1, 2)` are `type mismatch`. One argument is `argument count`. `==`, `!=`, and `+` on `[]const u8` are `type mismatch: expected an integer, found []const u8`. [`string.equals`](../std/string.md) and `string.compare` remain the runtime methods on `string_view`.

## Assembly

```asm
main:
movl $0xffffffff, -4(%rsp)       # constant
movl -4(%rsp), %eax              # move
retq                             # return
```

[Index](../README.md) · [Previous: panic](panic.md) · [Next: builtin/env/consts](family.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
