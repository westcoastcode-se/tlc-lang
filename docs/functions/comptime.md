# Comptime

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

This page is how tlc folds a `comptime` value and specializes a function while compiling.

## A comptime function

A `comptime func` calculates while the compiler is still compiling the program.

Every argument has to be a comptime value, even when the parameter is not marked `comptime`.

A `comptime func` calls a `comptime func`. A call of any other function is `not supported yet`.

A `new` or `delete` with no allocator uses the heap. That call is `not supported yet`. `new(&builtin.allocator)` and `delete(&builtin.allocator)` are that call.

`new(&arena)` is allowed when `arena.malloc`, `arena.free`, and `arena.realloc` do not call `malloc`, `realloc`, or `free`. A call that reaches one of those functions is `not supported yet`.

`from()` returns 42.

```c
type Point struct { x i32; y i32; }

var slot Point;
var used i32;

type Arena struct { mark i32; }

unsafe func Arena::malloc(self *this, size i32, align i32) ?*u8 {
    if (used != 0) { return null; }
    used = size;
    return ?*u8(&slot);
}

func Arena::free(self *this, ptr *u8) void {}

unsafe func Arena::realloc(self *this, ptr *u8, size i32, align i32) ?*u8 {
    return null;
}

comptime func from() i32 {
    var arena Arena;
    var p = new(&arena) Point;
    p.x = 40;
    p.y = 2;
    var sum = p.x + p.y;
    delete(&arena) p;
    return sum;
}

func main() i32 {
    return from();
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

A `return` leaves the `comptime func` and finishes the calculation for that call.

The call is an expression in the caller.

The compiler does not paste the function body into the caller at that expression.

## Comptime values

A comptime value is a folded integer, a constant `[]const u8`, or an enumerator.

A comptime value is also a `comptime const` of a struct, a float, an alias, or a function.

A string is a literal, a constant range, or a `comptime const` in the same file.

A `var` is not a comptime value.

A `const` is not a comptime value, until `comptime const folded = name`.

`value := 10` is a comptime value until a store or `&value` changes it.

`value := comptime_func()` is that binding when the call folds.

A use before the store stays a comptime value.

`value = 1`, `value += 1`, `++value`, and `&value` make a later use a runtime value.

A store in one arm of `if` drops the value after the `if`. The other arm still sees it.

A store in a loop drops the value for the whole loop.

A store in a `switch` drops the value for the whole `switch` and after it.

At file scope, `value := 10` is comptime when no function in the package stores it.

`export value := 10` is not a comptime value.

A `var` or a `const` passed to a `comptime func` is `not supported yet`.

A `var` or a `const` passed to a `comptime` parameter on an ordinary function is `type mismatch`.

```c
comptime func add(n i32) i32 {
    return n + 32;
}

func main() i32 {
    value := 10;
    return add(value);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

```c
comptime func add(n i32) i32 {
    return n + 32;
}

func main() i32 {
    value := 10;
    value = 3;
    return add(value);
}
```

`value = 3` before `add(value)` emits nothing. The call is `not supported yet`.

```c
comptime func add(n i32) i32 {
    return n + 32;
}

func main() i32 {
    value := add(10);
    return value;
}
```

```asm
main:
movl $42, -4(%rsp)               # value
movl -4(%rsp), %eax              # return
retq                             # return
```

A `comptime const` whose initializer folds to `i32` is a comptime argument, at file scope and inside a function.

That `comptime const` hides a `comptime const` of the same name.

A runtime `const` of a struct, a float, an alias, a function, a tuple, or `[]const u8` is not a comptime value.

A tuple of a folded integer, a `bool`, a constant `[]const u8`, an enumerator, or a `comptime const` is a comptime value.

A file-scope `const` whose initializer folds to `i32` is an element of that tuple. `const k = 40` then `field((k, 2))` is 42. The call is not emitted.

```c
const k = 40;

comptime func field(p (i32, i32)) i32 {
    return p[0] + p[1];
}

func main() i32 {
    return field((k, 2));
}
```

```asm
main:
movl $42, %eax                   # field((k, 2))
retq
```

The program returns 42.

A local `const` and a `var` in that tuple are `not supported yet`.

A local `comptime const` in that tuple is that value. That includes an array, `comptime const row = [_]i32{2, 0}`, and an alias, `comptime const other = Integer(4)`.

An element may be a `bool`, an `f64`, an `f32`, a struct, or a `[]const u8`.

An element may also be an enumerator, a tuple, a function, a pointer, or an array.

`comptime const pair = (40, 2)` names that tuple.

`comptime const nested = ((40, 2), 1)` names that tuple, and `nested[0][1]` is `2`.

`comptime const ops = (add, 1)` names a tuple whose element is that function, and `(ops[0])(40, 2)` calls `add`.

`comptime const pair = (&addr, 2)` names a tuple whose element is that pointer, and `*pair[0]` is `addr`.

`comptime const pair = (row, 0)` names that tuple when `row` is `[_]i32{40, 2}`.

The element is that array, and `pair[0][0]` is `40`.

A runtime `const` of a tuple is `type mismatch`.

```c
comptime func add(n i32) i32 {
    return n + 1;
}

func main() i32 {
    return add(41);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`comptime const n = 41` then `return add(n)` folds the same way.

A body with statements is compiled and run. The returned `i32` is that constant.

```c
comptime func add(n i32) i32 {
    var x = n;
    x = x + 1;
    return x;
}

func main() i32 {
    return add(41);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

A runtime `const` and a `var` are `not supported yet`.

```c
comptime func add(n i32) i32 {
    return n + 1;
}

func main() i32 {
    comptime const n = 41;
    return add(n);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

## When the call folds

The call folds when every argument is a comptime value and the result is `i32`, a `bool`, a struct, or a constant `[]const u8`, with no runtime symbol and no trace frame.

A body that is one `return` of a constant `i32` folds.

A body with statements is compiled and run, and that integer is the constant.

A struct result is kept as that literal.

A `[]const u8` result is that string.

`return "Hello"[0:5]` is `Hello`.

A body with statements is compiled and run, and the returned slice is that string.

`cut("Hello, world")` is `Hello` when the body keeps the bytes before `,`.

`cut("Hello, world").len` is `5`, and `cut("Hello, world")[1]` is `e`.

`printf("%s", cut("Hello, world"))` prints `Hello`.

```c
comptime func cut(text []const u8) []const u8 {
    return text[0:5];
}

func main() i32 {
    return cut("Hello, world").len;
}
```

```asm
main:
movl $5, %eax                    # constant
retq
```

`comptime const label = cut("Hello, world")` names that string.

`label[0]` is `H`.

A `var` or a `const` passed to that function is `not supported yet`. `comptime const text = "hi"` then `len(text)` is `2`.

A `bool` result stays `bool`. `comptime if (pos(3))` keeps that arm. A body with statements is compiled and run, and the returned `bool` is that constant.

```c
comptime func pos(n i32) bool {
    return n > 0;
}

func main() i32 {
    comptime if (pos(3)) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

## When a comptime call fails

A false `assert` is that message, on the `assert`.

An index outside a known length is `index out of range`, on that index.

The lines under the error are the call chain, newest first. A line is `file:line:column: name`. The name is the function that made the call. The first of those lines is the function that failed, at the `assert` or the index.

A call that folds still has no runtime symbol and no trace frame.

```c
comptime func add(n i32) i32 {
    assert(n > 0, "positive");
    return n;
}

func main() i32 {
    return add(0);
}
```

`add(0)` emits nothing. The error is `positive` at the `assert`. The chain under it is:

```text
main.tlc:2:5: add
main.tlc:7:12: main
```

```c
comptime func at(n i32) i32 {
    return "hi"[n];
}

func main() i32 {
    return at(5);
}
```

`at(5)` emits nothing. The error is `index out of range`, then `main.tlc:2:12: at` and `main.tlc:6:12: main`.

A tuple result is that tuple. `pair(40)[0] + pair(40)[1]` is 82. `comptime const got = pair(40)` names it. `var got = pair(40)` copies that folded tuple.

```c
comptime func pair(n i32) ((i32, i32)) {
    var a = n;
    var b = a + 2;
    return (a, b);
}

func main() i32 {
    comptime const got = pair(40);
    return got[0] + got[1];
}
```

```asm
main:
movl $82, %eax                   # got[0] + got[1]
retq
```

`var got = pair(40)` is that same sum. The call is not emitted.

```c
func main() i32 {
    var got = pair(40);
    return got[0] + got[1];
}
```

```asm
main:
movl $82, %eax                   # got[0] + got[1]
retq
```

The program returns 82.

A `comptime func` may return `f64`. `bias(1)` is `0.5`. A body that is one `return` of a constant folds. A body with statements is compiled and run. The call is not emitted.

```c
comptime func bias(n i32) f64 {
    var half = 0.5;
    if (n > 0) {
        return half;
    }
    return 0.0;
}

func main() i32 {
    var x = bias(1);
    if (x == 0.5) {
        return 42;
    }
    return 1;
}
```

```asm
main:
movsd (%rip), %xmm0              # bias(1)
movsd %xmm0, -8(%rsp)            # x
movsd -8(%rsp), %xmm0            # x
movsd (%rip), %xmm1              # 0.5
ucomisd %xmm1, %xmm0             # x == 0.5
jne .Lother
jp .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

`f32` is that result too. `bias(1)` is `0.5f`. A body that is one `return` of a constant folds. A body with statements is compiled and run. The call is not emitted.

```c
comptime func bias(n i32) f32 {
    var h = 0.5f;
    if (n > 0) {
        return h;
    }
    return 0.0f;
}

func main() i32 {
    var x = bias(1);
    if (x == 0.5f) {
        return 42;
    }
    return 1;
}
```

```asm
main:
movss (%rip), %xmm0              # bias(1)
movss %xmm0, -4(%rsp)            # x
movss -4(%rsp), %xmm0            # x
movss (%rip), %xmm1              # 0.5f
ucomiss %xmm1, %xmm0             # x == 0.5f
jne .Lother
jp .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

An enumerator is that result. `pick(1)` is `Order.equal`. A body with statements is compiled and run. The call is not emitted.

```c
type Order enum {
    less,
    equal,
    greater,
};

comptime func pick(n i32) Order {
    if (n > 0) {
        return Order.equal;
    }
    return Order.less;
}

func main() i32 {
    var x = pick(1);
    if (x == Order.equal) {
        return 42;
    }
    return 1;
}
```

```asm
main:
movl $1, -4(%rsp)                # x = Order.equal
cmpl $1, -4(%rsp)                # x == Order.equal
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

`[2]i32` is that result. `row(40)` is `{40, 42}`. A slice and a pointer stay out. The call is not emitted.

```c
comptime func row(n i32) [2]i32 {
    var a = n;
    var b = a + 2;
    return [_]i32{a, b};
}

func main() i32 {
    var r = row(40);
    return r[0] + r[1];
}
```

```asm
main:
subq $24, %rsp                   # allocate
movl $40, 16(%rsp)               # r[0]
movl $42, 20(%rsp)               # r[1]
movl 16(%rsp), %eax              # r[0]
addl 20(%rsp), %eax              # r[0] + r[1]
addq $24, %rsp                   # release
retq
```

The program returns 82.

`i64` is that result. `wide(0)` is `3000000000`. A body with statements is compiled and run. The call is not emitted.

```c
comptime func wide(n i32) i64 {
    var k = n;
    return 3000000000 + i64(k);
}

func main() i32 {
    var x = wide(0);
    if (x == 3000000000) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movl $3000000000, %eax
movq %rax, -8(%rsp)              # x
movl $3000000000, %eax
cmpq %rax, -8(%rsp)              # x == 3000000000
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42. `u32` is the same call. `wide(0)` is `3000000000u32`.

`u64` is that result. `wide(0)` is `4u64`. A body with statements is compiled and run. The call is not emitted. `comptime if (n > 0)` keeps that arm when `n` is `4u64`. `comptime if (wide(0) > 0u64)` keeps it too. `comptime switch (n)` on `4u64` keeps that arm. `3000000000u64`, `3000000000u32`, and `-3000000000` keep that arm too. `main` is `movl $42, %eax` and `retq`. A `var` is `not supported yet`. An ordinary `switch` on a `u64` is `type mismatch: expected i32, found u64`.

```c
comptime func wide(n i32) u64 {
    var k = n;
    return 4u64 + u64(k);
}

func main() i32 {
    var x = wide(0);
    if (x == 4u64) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movq $4, -8(%rsp)                # x
cmpq $4, -8(%rsp)                # x == 4u64
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

`[2]f32` is that result. `row()` is `{0.5f, 1.0f}`. `[2]u64` is that result. `row(0)[0]` is `4u64`. `[2]i16`, `[2]u16`, `[2]i8`, and `[2]u8` are that call. `[2][2]i64`, `[2][2]u32`, `[2][2]u64`, `[2][2]f64`, `[2][2]f32`, and `[2][2]bool` are that call. `[2][2]i8`, `[2][2]u8`, `[2][2]i16`, and `[2][2]u16` are that call. `[2][2][2]i32` is that call, and so are `i64`, `u32`, `u64`, `f64`, `f32`, and `bool`. `[2][2][2]i8`, `[2][2][2]u8`, `[2][2][2]i16`, and `[2][2][2]u16` are that call. `[]i32` and `[]u8` are that call. `i16`, `u16`, `i8`, and `u8` are results too. `[2][2][2][2]i8`, `[2][2][2][2]u8`, `[2][2][2][2]i32`, `[]Point`, `[][2]i32`, and `[][2][2]i32` are that call. A pointer to a file-scope name is that call. `&p.x` as `*i32` is that call. `[2][2][2][2]u16` and `[][2][2][2]i32` stay out. `**i32` and `?*i32` stay out. `200i8` and `40000i16` are `value does not fit`. `200u8` keeps its bits.

`[2]bool` is that result. `flags(1)` stores `true` in `r[0]`. The call is not emitted. `r[1]` is not read, so that store is removed.

```c
comptime func flags(n i32) [2]bool {
    var a = true;
    var b = n == 0;
    return [_]bool{a, b};
}

func main() i32 {
    var r = flags(1);
    if (r[0]) {
        return 42;
    }
    return 0;
}
```

```asm
main:
pushq %rax                       # allocate
movb $1, 6(%rsp)                 # r[0]
movb 6(%rsp), %al                # r[0]
andb $1, %al
movzbl %al, %eax
cmpl $0, %eax                    # r[0]
jne .Lyes
xorl %eax, %eax                  # other arm
popq %rcx
retq
.Lyes:
movl $42, %eax                   # return
popq %rcx
retq
```

The program returns 42.

`[2]i64` is that result. `row(0)[0]` is `3000000000`. `[2]u32` is the same call with `3000000000u32`.

```c
comptime func row(n i32) [2]i64 {
    var a = 3000000000 + i64(n);
    var b = 2i64;
    return [_]i64{a, b};
}

func main() i32 {
    var r = row(0);
    if (r[0] == 3000000000) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # allocate
movl $3000000000, %eax
movq %rax, 8(%rsp)               # r[0]
movl $3000000000, %eax
cmpq %rax, 8(%rsp)               # r[0] == 3000000000
je .Lyes
xorl %eax, %eax                  # other arm
addq $24, %rsp
retq
.Lyes:
movl $42, %eax                   # return
addq $24, %rsp
retq
```

The program returns 42.

`[2]f64` is that result. `row()[0]` is `0.5`.

```c
comptime func row() [2]f64 {
    var a = 0.5;
    var b = 1.0;
    return [_]f64{a, b};
}

func main() i32 {
    var r = row();
    if (r[0] == 0.5) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # allocate
movsd (%rip), %xmm0              # r[0]
movsd %xmm0, 8(%rsp)
movsd 8(%rsp), %xmm0
movsd (%rip), %xmm1              # 0.5
ucomisd %xmm1, %xmm0
jne .Lother
jp .Lother
movl $42, %eax                   # return
addq $24, %rsp                   # release
retq
```

The program returns 42.

`[2][2]i32` is that result. `grid(40)` stores 40 and 42. `g[0][0] + g[1][0]` is 82. The call is not emitted. `return [_][2]i32{{40, 1}, {2, 0}}` then `g[0][0] + g[1][0]` is 42.

```c
comptime func grid(n i32) [2][2]i32 {
    var a = n;
    var b = a + 2;
    return [_][2]i32{{a, 1}, {b, 0}};
}

func main() i32 {
    var g = grid(40);
    return g[0][0] + g[1][0];
}
```

```asm
main:
subq $56, %rsp                   # allocate
movl $40, 40(%rsp)               # g[0][0]
movl $42, 48(%rsp)               # g[1][0]
leaq 40(%rsp), %rax              # g[0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movl (%rax), %eax                # g[0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # g[1]
addq $8, %rax
movq %rax, 16(%rsp)
movq 16(%rsp), %rcx
movl 12(%rsp), %eax
addl (%rcx), %eax                # g[0][0] + g[1][0]
addq $56, %rsp                   # release
retq
```

The program returns 82.

`[2]u64` is that result. `row(0)` stores `4` in `r[0]`. The other element is not read, so that store is removed. The call is not emitted.

```c
comptime func row(n i32) [2]u64 {
    var k = n;
    return [_]u64{4u64 + u64(k), 2u64};
}

func main() i32 {
    var r = row(0);
    if (r[0] == 4u64) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # allocate
movq $4, 8(%rsp)                 # r[0]
cmpq $4, 8(%rsp)                 # r[0] == 4u64
je .Lyes
.Lyes:
movl $42, %eax                   # return
addq $24, %rsp                   # release
retq
```

The program returns 42.

`[2][2]i64` is that result. `grid(40)` stores 40 in `g[0][0]`. `[2][2]u32` is the same call. `return [_][2]i64{{40, 1}, {2, 0}}` then `g[0][0] + g[1][0]` is 42.

```c
comptime func grid(n i32) [2][2]i64 {
    var a = n;
    return [_][2]i64{{i64(a), 1}, {2, 0}};
}

func main() i32 {
    var g = grid(40);
    if (g[0][0] == 40) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $56, %rsp                   # allocate
movq $40, 24(%rsp)               # g[0][0]
leaq 24(%rsp), %rax              # g[0]
movq %rax, 16(%rsp)
movq 16(%rsp), %rax
cmpq $40, (%rax)                 # g[0][0] == 40
je .Lyes
.Lyes:
movl $42, %eax                   # return
addq $56, %rsp                   # release
retq
```

The program returns 42.

`[2][2]i8` is that result. `grid(40)` stores 40 and 2. `g[0][0] + g[0][1]` is 42. `[2][2]u8`, `[2][2]i16`, and `[2][2]u16` are the same call. `200u8` keeps its bits. The call is not emitted.

```c
comptime func grid(n i32) [2][2]i8 {
    var a = n;
    return [_][2]i8{{i8(a), 2i8}, {1i8, 0i8}};
}

func main() i32 {
    var g = grid(40);
    return g[0][0] + g[0][1];
}
```

```asm
main:
subq $40, %rsp                   # allocate
movb $40, 36(%rsp)               # g[0][0]
movb $2, 37(%rsp)                # g[0][1]
leaq 36(%rsp), %rax              # g[0]
movq %rax, 24(%rsp)
movq 24(%rsp), %rax
movsbl (%rax), %eax              # g[0][0]
movl %eax, 4(%rsp)
leaq 36(%rsp), %rax              # g[0]
movq %rax, 8(%rsp)
movl 4(%rsp), %eax
movq 8(%rsp), %rcx
movsbl 1(%rcx), %ecx             # g[0][1]
addl %ecx, %eax                  # g[0][0] + g[0][1]
addq $40, %rsp                   # release
retq
```

The program returns 42.

`[2][2][2]i32` is that result. `cube(40)` stores 40 and 2. `c[0][0][0] + c[0][1][0]` is 42. `i64`, `u32`, `u64`, `f64`, `f32`, and `bool` are the same call. The call is not emitted.

```c
comptime func cube(n i32) [2][2][2]i32 {
    var a = n;
    return [_][2][2]i32{{{a, 1}, {2, 0}}, {{3, 4}, {5, 6}}};
}

func main() i32 {
    var c = cube(40);
    return c[0][0][0] + c[0][1][0];
}
```

```asm
main:
subq $72, %rsp                   # allocate
movl $40, 40(%rsp)               # c[0][0][0]
movl $2, 48(%rsp)                # c[0][1][0]
leaq 40(%rsp), %rax              # c[0][0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movl (%rax), %eax                # c[0][0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # c[0][1]
addq $8, %rax
movq %rax, 16(%rsp)
movq 16(%rsp), %rcx
movl 12(%rsp), %eax
addl (%rcx), %eax                # c[0][0][0] + c[0][1][0]
addq $72, %rsp                   # release
retq
```

The program returns 42.

`[2][2][2][2]i32` is that result. `deep(40)` stores 40 and 2. `c[0][0][0][0] + c[0][0][1][0]` is 42. The call is not emitted. `[2][2][2][2]u8` is that call. `[2][2][2][2]u16` stays `not supported yet`.

```c
comptime func deep(n i32) [2][2][2][2]i32 {
    var a = n;
    return [_][2][2][2]i32{{{{a, 1}, {2, 0}}, {{3, 4}, {5, 6}}}, {{{7, 8}, {9, 10}}, {{11, 12}, {13, 14}}}};
}

func main() i32 {
    var c = deep(40);
    return c[0][0][0][0] + c[0][0][1][0];
}
```

```asm
main:
subq $104, %rsp                  # allocate
movl $40, 40(%rsp)               # c[0][0][0][0]
movl $2, 48(%rsp)                # c[0][0][1][0]
leaq 40(%rsp), %rax              # c[0][0][0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movl (%rax), %eax                # c[0][0][0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # c[0][0][1]
addq $8, %rax
movq %rax, 16(%rsp)
movq 16(%rsp), %rcx
movl 12(%rsp), %eax
addl (%rcx), %eax                # c[0][0][0][0] + c[0][0][1][0]
addq $104, %rsp                  # release
retq
```

The program returns 42.

`[2][2][2][2]u8` is that result. `grid(40)` stores 40 and 2. `c[0][0][0][0] + c[0][0][0][1]` is 42. The call is not emitted. `[2][2][2][2]u16` stays `not supported yet`.

```c
comptime func grid(n i32) [2][2][2][2]u8 {
    var a = n;
    return [_][2][2][2]u8{{{{u8(a), 2u8}, {1u8, 0u8}}, {{3u8, 4u8}, {5u8, 6u8}}}, {{{7u8, 8u8}, {9u8, 10u8}}, {{11u8, 12u8}, {13u8, 14u8}}}};
}

func main() i32 {
    var c = grid(40);
    return c[0][0][0][0] + c[0][0][0][1];
}
```

```asm
main:
subq $56, %rsp                   # allocate
movb $40, 40(%rsp)               # c[0][0][0][0]
movb $2, 41(%rsp)                # c[0][0][0][1]
leaq 40(%rsp), %rax              # c[0][0][0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movzbl (%rax), %eax              # c[0][0][0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # c[0][0][0]
movq %rax, 16(%rsp)
movl 12(%rsp), %eax
movq 16(%rsp), %rcx
movzbl 1(%rcx), %ecx             # c[0][0][0][1]
addl %ecx, %eax                  # c[0][0][0][0] + c[0][0][0][1]
addq $56, %rsp                   # release
retq
```

The program returns 42.

`[2][2][2]i8` is that result. `grid(40)` stores 40 and 2. `c[0][0][0] + c[0][0][1]` is 42. `[2][2][2]u8`, `[2][2][2]i16`, and `[2][2][2]u16` are the same call. `200u8` keeps its bits. The call is not emitted.

```c
comptime func grid(n i32) [2][2][2]i8 {
    var a = n;
    return [_][2][2]i8{{{i8(a), 2i8}, {1i8, 0i8}}, {{3i8, 4i8}, {5i8, 6i8}}};
}

func main() i32 {
    var c = grid(40);
    return c[0][0][0] + c[0][0][1];
}
```

```asm
main:
subq $40, %rsp                   # allocate
movb $40, 32(%rsp)               # c[0][0][0]
movb $2, 33(%rsp)                # c[0][0][1]
leaq 32(%rsp), %rax              # c[0][0]
movq %rax, 24(%rsp)
movq 24(%rsp), %rax
movsbl (%rax), %eax              # c[0][0][0]
movl %eax, 4(%rsp)
leaq 32(%rsp), %rax              # c[0][0]
movq %rax, 8(%rsp)
movl 4(%rsp), %eax
movq 8(%rsp), %rcx
movsbl 1(%rcx), %ecx             # c[0][0][1]
addl %ecx, %eax                  # c[0][0][0] + c[0][0][1]
addq $40, %rsp                   # release
retq
```

The program returns 42.

`[2][2][2][2]i8` is that result. `grid(40)` stores 40 and 2. `c[0][0][0][0] + c[0][0][0][1]` is 42. The call is not emitted. `[2][2][2][2]u8` is that call. `[2][2][2][2]u16` stays `not supported yet`.

```c
comptime func grid(n i32) [2][2][2][2]i8 {
    var a = n;
    return [_][2][2][2]i8{{{{i8(a), 2i8}, {1i8, 0i8}}, {{3i8, 4i8}, {5i8, 6i8}}}, {{{7i8, 8i8}, {9i8, 10i8}}, {{11i8, 12i8}, {13i8, 14i8}}}};
}

func main() i32 {
    var c = grid(40);
    return c[0][0][0][0] + c[0][0][0][1];
}
```

```asm
main:
subq $56, %rsp                   # allocate
movb $40, 40(%rsp)               # c[0][0][0][0]
movb $2, 41(%rsp)                # c[0][0][0][1]
leaq 40(%rsp), %rax              # c[0][0][0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movsbl (%rax), %eax              # c[0][0][0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # c[0][0][0]
movq %rax, 16(%rsp)
movl 12(%rsp), %eax
movq 16(%rsp), %rcx
movsbl 1(%rcx), %ecx             # c[0][0][0][1]
addl %ecx, %eax                  # c[0][0][0][0] + c[0][0][0][1]
addq $56, %rsp                   # release
retq
```

The program returns 42.

`[]i32` is that result. `cut(40)` stores 40 and 2. `s[0] + s[1]` is 42. `[]u8` is the same call, and so is a slice of the other scalars. `[]const u8` stays the string. The call is not emitted. `[]Point`, `[][2]i32`, and `[][2][2]i32` are that call. A pointer to a file-scope name is that call. `&p.x` as `*i32` is that call. `**i32` and `?*i32` stay `not supported yet`. `[][2][2][2]i32` stays `not supported yet`.

`sum([_]i32{40, 2})` when `sum` takes `[]i32` and returns `s[0] + s[1]` is `movl $42, %eax` and `retq`. `[]u8` of `[_]u8{40u8, 2u8}` is that call. A `var` is `not supported yet`. `const row = [_]i32{40, 2}` then `sum(row)` is `type mismatch: expected []i32, found [2]i32`.

```c
comptime func cut(n i32) []i32 {
    var a = n;
    return [_]i32{a, 2};
}

func main() i32 {
    var s = cut(40);
    return s[0] + s[1];
}
```

```asm
main:
subq $24, %rsp                   # allocate
movl $40, 16(%rsp)               # s[0]
movl $2, 20(%rsp)                # s[1]
movl 16(%rsp), %eax              # s[0]
movl %eax, 8(%rsp)
movl 8(%rsp), %eax
addl 20(%rsp), %eax              # s[0] + s[1]
addq $24, %rsp                   # release
retq
```

The program returns 42.

`cut(40).len` and `s.len` are the element count. Three elements are `movl $3, %eax` and `retq`. `.len()` is that count. An empty array is 0.

`[]Point` is that result. `pts(40)` stores 40 and 2. `s[0].x + s[0].y` is 42. The call is not emitted. `[][2][2]i32` is that call. `[][2][2][2]i32` stays `not supported yet`.

```c
type Point struct { x i32; y i32; }

comptime func pts(n i32) []Point {
    var x = n;
    return [_]Point{Point{.x = x, .y = 2}};
}

func main() i32 {
    var s = pts(40);
    return s[0].x + s[0].y;
}
```

```asm
main:
subq $40, %rsp                   # allocate
movl $40, 32(%rsp)               # s[0].x
movl $2, 36(%rsp)                # s[0].y
movl 32(%rsp), %eax              # s[0].x
movl 36(%rsp), %ecx              # s[0].y
movl %ecx, 28(%rsp)
movl %eax, 24(%rsp)
movl 24(%rsp), %eax
addl 28(%rsp), %eax              # s[0].x + s[0].y
addq $40, %rsp                   # release
retq
```

The program returns 42.

`[][2]i32` is that result. `rows(40)` stores 40 and 2. `s[0][0] + s[0][1]` is 42. The call is not emitted.

```c
comptime func rows(n i32) [][2]i32 {
    var a = n;
    return [_][2]i32{{a, 2}, {1, 0}};
}

func main() i32 {
    var s = rows(40);
    return s[0][0] + s[0][1];
}
```

```asm
main:
subq $56, %rsp                   # allocate
movl $40, 40(%rsp)               # s[0][0]
movl $2, 44(%rsp)                # s[0][1]
leaq 40(%rsp), %rax              # s[0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movl (%rax), %eax                # s[0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # s[0]
movq %rax, 16(%rsp)
movq 16(%rsp), %rcx
movl 12(%rsp), %eax
addl 4(%rcx), %eax               # s[0][0] + s[0][1]
addq $56, %rsp                   # release
retq
```

The program returns 42.

`[][2][2]i32` is that result. `rows(40)` stores 40 and 2. `s[0][0][0] + s[0][0][1]` is 42. The call is not emitted. `[][2][2][2]i32` stays `not supported yet`.

```c
comptime func rows(n i32) [][2][2]i32 {
    var a = n;
    return [_][2][2]i32{{{a, 2}, {1, 0}}};
}

func main() i32 {
    var s = rows(40);
    return s[0][0][0] + s[0][0][1];
}
```

```asm
main:
subq $56, %rsp                   # allocate
movl $40, 40(%rsp)               # s[0][0][0]
movl $2, 44(%rsp)                # s[0][0][1]
leaq 40(%rsp), %rax              # s[0][0]
movq %rax, 32(%rsp)
movq 32(%rsp), %rax
movl (%rax), %eax                # s[0][0][0]
movl %eax, 12(%rsp)
leaq 40(%rsp), %rax              # s[0][0]
movq %rax, 16(%rsp)
movq 16(%rsp), %rcx
movl 12(%rsp), %eax
addl 4(%rcx), %eax               # s[0][0][0] + s[0][0][1]
addq $56, %rsp                   # release
retq
```

The program returns 42.

A pointer to a file-scope name is that result. `give()` is `&n`. `*p` is 42. The call is not emitted. A local `var` stays `not supported yet`. `**i32` and `?*i32` stay `not supported yet`. `&p.x` as `*i32` is that call. `&p.y` is the same call. `&p` as `*Point` stays the address. A pointer field stays `global initializer is not a constant`.

```c
var n i32 = 42;

comptime func give() *i32 {
    return &n;
}

func main() i32 {
    var p = give();
    return *p;
}
```

```asm
main:
leaq main.n(%rip), %rax          # &n
movq %rax, -8(%rsp)              # p
movq -8(%rsp), %rax
movl (%rax), %eax                # *p
retq
```

The program returns 42.

`&p.x` as `*i32` is that result. `*ex() + *why()` is 42. The calls are not emitted. `**i32` and `?*i32` stay `not supported yet`.

```c
type Point struct { x i32; y i32; }

var p = Point{.x = 40, .y = 2};

comptime func ex() *i32 {
    return &p.x;
}

comptime func why() *i32 {
    return &p.y;
}

func main() i32 {
    return *ex() + *why();
}
```

```asm
main:
movl main.p(%rip), %eax          # p.x
addl main.p+4(%rip), %eax        # p.x + p.y
retq
```

The program returns 42.

`i16` is that result. `wide(40) + 2i16` is 42. `u16` is the same call. The value is stored as an `i32`.

```c
comptime func wide(n i32) i16 {
    var k = n;
    return i16(k);
}

func main() i32 {
    var x = wide(40);
    if (x + 2i16 == 42i16) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movl $40, -4(%rsp)               # x
movl -4(%rsp), %eax
addl $2, %eax                    # x + 2i16
cmpl $42, %eax
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42. `wide(200)` when `wide` returns `u8` stores `movl $200, -4(%rsp)` and compares that with `200u8`.

`[2]i16` is that result. `row(40)[0] + row(40)[1]` is 42. `[2]u16` is the same call.

```c
comptime func row(n i32) [2]i16 {
    var k = n;
    return [_]i16{i16(k), 2i16};
}

func main() i32 {
    var r = row(40);
    if (r[0] + r[1] == 42i16) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # allocate
movw $40, 20(%rsp)               # r[0]
movw $2, 22(%rsp)                # r[1]
movswl 20(%rsp), %eax            # r[0]
movl %eax, 12(%rsp)
movl 12(%rsp), %eax
movswl 22(%rsp), %ecx            # r[1]
addl %ecx, %eax                  # r[0] + r[1]
cmpl $42, %eax
je .Lyes
.Lyes:
movl $42, %eax                   # return
addq $24, %rsp                   # release
retq
```

The program returns 42. `[2]u8` stores the low byte of `200`. That byte is `movb $-56, 6(%rsp)`. The load is `movzbl` and `cmpl $200, %eax`.

`[2][2]f64` is that result. `grid(40)` stores 40 in `g[0][0]`. `[2][2]f32`, `[2][2]bool`, and `[2][2]u64` are that call.

```c
comptime func grid(n i32) [2][2]f64 {
    var a = n;
    return [_][2]f64{{f64(a), 1.0}, {2.0, 0.0}};
}

func main() i32 {
    var g = grid(40);
    if (g[0][0] == 40.0) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $56, %rsp                   # allocate
movsd (%rip), %xmm0              # g[0][0]
movsd %xmm0, 24(%rsp)
leaq 24(%rsp), %rax              # g[0]
movq %rax, 16(%rsp)
movq 16(%rsp), %rax
movsd (%rax), %xmm0              # g[0][0]
movsd (%rip), %xmm1              # 40.0
ucomisd %xmm1, %xmm0
jne .Lother
jp .Lother
movl $42, %eax                   # return
addq $56, %rsp                   # release
retq
```

The program returns 42.

`enum(i8)` is that result. `pick(1)` is `Tiny.b`. `enum(u8)` is the same call. `enum(bool)` and `enum(i64)` are that call. `enum(i16)`, `enum(u16)`, `enum(u32)`, and `enum(u64)` are that call. A member value that does not fit, `b = 3000000000` on `enum(i64)`, is `not supported yet`. `b = 40000` on `enum(i16)` is `type mismatch`. `b = 3000000000u32` on `enum(u32)` and `b = 4u64` on `enum(u64)` are `not supported yet` on the value.

```c
type Tiny enum(i8) { a, b }

comptime func pick(n i32) Tiny {
    var k = n;
    if (k == 1) { return Tiny.b; }
    return Tiny.a;
}

func main() i32 {
    var x = pick(1);
    if (x == Tiny.b) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movb $1, -1(%rsp)                # x = Tiny.b
movsbl -1(%rsp), %eax
cmpl $1, %eax                    # x == Tiny.b
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

`enum(bool)` is that result. `pick(1)` is `Flag.yes`.

```c
type Flag enum(bool) { no, yes }

comptime func pick(n i32) Flag {
    var k = n;
    if (k == 1) { return Flag.yes; }
    return Flag.no;
}

func main() i32 {
    var x = pick(1);
    if (x == Flag.yes) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movb $1, -1(%rsp)                # x = Flag.yes
movb -1(%rsp), %al
andb $1, %al
movzbl %al, %eax
cmpl $1, %eax                    # x == Flag.yes
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

`enum(i64)` is that result. `pick(1)` is `Wide.b`.

```c
type Wide enum(i64) { a, b }

comptime func pick(n i32) Wide {
    var k = n;
    if (k == 1) { return Wide.b; }
    return Wide.a;
}

func main() i32 {
    var x = pick(1);
    if (x == Wide.b) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movq $1, -8(%rsp)                # x = Wide.b
cmpq $1, -8(%rsp)                # x == Wide.b
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

`enum(i16)` is that result. `pick(1)` is `Tiny.b`.

```c
type Tiny enum(i16) { a, b }

comptime func pick(n i32) Tiny {
    var k = n;
    if (k == 1) { return Tiny.b; }
    return Tiny.a;
}

func main() i32 {
    var x = pick(1);
    if (x == Tiny.b) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movw $1, -2(%rsp)                # x = Tiny.b
movswl -2(%rsp), %eax
cmpl $1, %eax                    # x == Tiny.b
jne .Lother
movl $42, %eax                   # return
retq
```

The program returns 42.

```c
comptime func row() [2]f32 {
    var a = 0.5f;
    var b = 1.0f;
    return [_]f32{a, b};
}

func main() i32 {
    var r = row();
    if (r[0] == 0.5f) {
        return 42;
    }
    return 0;
}
```

```asm
main:
subq $24, %rsp                   # allocate
movss (%rip), %xmm0              # r[0]
movss %xmm0, 16(%rsp)
movss 16(%rsp), %xmm0
movss (%rip), %xmm1              # 0.5f
ucomiss %xmm1, %xmm0
jne .Lother
jp .Lother
movl $42, %eax                   # return
addq $24, %rsp                   # release
retq
```

The program returns 42.

A result that is not `i32`, not a `bool`, not an `f32`, not an `f64`, not an `i64`, not a `u32`, not a `u64`, not an `i16`, not a `u16`, not an `i8`, not a `u8`, not an enumerator, not a struct, not a tuple, not an `[N]i32`, not an `[N]i64`, not an `[N]u32`, not an `[N]u64`, not an `[N]f32`, not an `[N]f64`, not an `[N]i16`, not an `[N]u16`, not an `[N]i8`, not an `[N]u8`, not an `[N]bool`, not a `[2][2]i32`, not a `[2][2]i64`, not a `[2][2]u32`, not a `[2][2]u64`, not a `[2][2]f64`, not a `[2][2]f32`, not a `[2][2]bool`, not a `[2][2]i8`, not a `[2][2]u8`, not a `[2][2]i16`, not a `[2][2]u16`, not a `[2][2][2]i32`, not a `[2][2][2]i64`, not a `[2][2][2]u32`, not a `[2][2][2]u64`, not a `[2][2][2]f64`, not a `[2][2][2]f32`, not a `[2][2][2]bool`, not a `[]i32`, not a `[]u8`, not a `[2][2][2]i8`, not a `[2][2][2]u8`, not a `[2][2][2]i16`, not a `[2][2][2]u16`, not a `[2][2][2][2]i32`, not a `[2][2][2][2]i8`, not a `[2][2][2][2]u8`, not a `[]Point`, not a `[][2]i32`, not a `[][2][2]i32`, not a pointer to a file-scope name, not `&p.x` as `*i32`, and not a constant `[]const u8` is `not supported yet`. An enumerator whose underlying type is `i8`, `u8`, `i16`, `u16`, `u32`, `bool`, `i64`, or `u64` is that call. `[2][2][2][2]u16`, `[][2][2][2]i32`, `**i32`, and `?*i32` stay `not supported yet`.

`emit` is not valid in a `comptime func`.

A type parameter, `extern`, and `export` are `not supported yet`. A method is described below.

A call that does not fold is `not supported yet`.

## Conditions and switches

`comptime { ... }` inside an ordinary function keeps the statements that are compile-time.

A `return` of a compile-time value ends the function. A later statement is not emitted.

A `var`, a runtime `const`, a call, and a loop inside that block are `not supported yet`. A local `comptime func` is `not supported yet`.

```c
func main() i32 {
    comptime {
        return 1;
    }
    return 0;
}
```

```asm
main:
movl $1, %eax                    # constant
retq                             # return
```

`return 0` is not emitted.

`comptime for` unrolls when the bounds fold, inside a [macro](macro.md) and outside one.

A bound that does not fold is `not supported yet`. It emits nothing.

`const for` is not a form.

`comptime if` keeps one arm when the condition folds.

A local `const` whose initializer folds to `i32` is that condition.

`const n = 1` then `comptime if (n == 1)` returns 42.

```c
func main() i32 {
    const n = 1;
    comptime if (n == 1) {
        return 42;
    }
    return 0;
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`const m = n + 1` is that sum.

`const n i32 = 1` is that binding.

`const n = -1` then `comptime if (n == -1)` is those same two instructions.

A local `const` hides a file-scope `const` of the same name.

A `var` does not fold.

`var n const i32` does not fold.

An initializer that does not fold is `not supported yet`.

A condition that is not comptime is `not supported yet`.

Inside a `comptime func`, a parameter is not a constant until the call substitutes it.

`comptime if`, `comptime switch`, and `comptime for` on that parameter wait for the call. The body typechecks every arm. The call keeps the selected arm, and `comptime for` unrolls.

`value := n` and `comptime const k = n` are that parameter. A store into `value` makes a later use a runtime value.

`var buf [n]i32`, `var buf [k]i32`, and `var buf [value]i32` use that integer as the length. `buf.len` is `n`. A negative value is `array length must be >= 0`. A `var` is not a length.

`func ordinary(comptime n i32)` uses that parameter as `var row [n]i32`. `row.len` is `n`. The result `[n]i32` is that array. `ordinary(1)[0]` is 40. `func fill(comptime n i32, value i32) [n]i32` is that array too. `fill(1, 40)[0]` is 40. `value` is passed. `n` is not. A negative value is `array length must be >= 0`. A `var` is not a length. A length whose other elements are not stored is `uninitialized`.

`var buf Buf[n]`, `var buf Buf[k]`, and `var buf Buf[value]` write that integer as a type argument. `buf.raw.len` is `n` when `raw` is `[N]u8`. `Buf[n]{}` and `new(&arena) Buf[n]` are that same type. `type Mine[N i32] Buf[N]` then `var buf Mine[n]`, `Mine[n]{}`, and `new(&arena) Mine[n]` are that alias. `Mine` and `Buf` stay different types. `new Buf[n]` and `new Mine[n]` use the heap and are `not supported yet`. A negative value is `array length must be >= 0`. A `var` is `type mismatch`.

`width(4)` returns 4.

```c
comptime func width(n i32) i32 {
    var buf [n]i32;
    return buf.len;
}

func main() i32 {
    return width(4);
}
```

```asm
main:
movl $4, %eax                    # constant
retq                             # return
```

`pick(1)` returns 42.

```c
comptime func pick(n i32) i32 {
    comptime if (n == 1) {
        return 42;
    }
    return 0;
}

func main() i32 {
    return pick(1);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

A `var` in that condition does not fold.

`from(4)` returns 4.

```c
type Buf[N i32] struct { raw [N]u8; }

comptime func from(n i32) i32 {
    var buf Buf[n];
    return buf.raw.len;
}

func main() i32 {
    return from(4);
}
```

```asm
main:
movl $4, %eax                    # constant
retq                             # return
```

`Mine[n]` is that alias. `from(4)` returns 4. `Mine[4]` is a different type from `Buf[4]`.

```c
type Buf[N i32] struct { raw [N]u8; }

type Mine[N i32] Buf[N];

comptime func from(n i32) i32 {
    var buf Mine[n];
    return buf.raw.len;
}

func main() i32 {
    return from(4);
}
```

```asm
main:
movl $4, %eax                    # constant
retq                             # return
```

A `comptime` parameter is comptime.

The copy that still receives it typechecks both arms.

The copy that substitutes the value keeps one arm.

An ordinary `if` typechecks both arms. It may use a comptime value, including `comptime const n = 1` and `value := 1`.

`comptime switch` keeps the matching case and the statements that follow until `break` when the value folds.

A value that is not comptime is `not supported yet`.

A `comptime` parameter leaves every case until a call substitutes it.

An ordinary `switch` typechecks every case. It may use a comptime value, including `comptime const n = 1` and `value := 1`.

A `var` does not fold for `comptime switch` or `comptime for`.

A local `const` whose initializer folds to `i32` is a `comptime switch` value and a `comptime for` bound.

In a [macro](macro.md), `comptime if` keeps one arm when the condition folds. A condition that does not fold is `not supported yet`. `comptime switch` keeps the matching case and the statements that follow until `break` when the value folds. A value that does not fold is `not supported yet`. `comptime for` unrolls when the bounds fold. A bound that does not fold is `not supported yet`.

A local `const` whose initializer folds to `i32` is a `comptime if` condition, a `comptime switch` value, and a `comptime for` bound there.

In a macro, a plain `for` unrolls when the bounds fold and runs at run time when they do not. A plain `if` keeps one arm when the condition folds and runs at run time when it does not.

A `switch` in a macro keeps the matching case and the statements that follow until `break` when the value folds. The other cases are not typechecked and not emitted. A value that does not fold runs at run time, and every case is typechecked.

## A comptime parameter on an ordinary function

An ordinary function may mark a parameter `comptime`.

That argument has to be a comptime value, or the call is `type mismatch`.

When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds.

The compiler still compiles that function.

A mix of a `comptime` parameter and a runtime parameter is specialized per constant.

The compiler does not pass the comptime parameter.

Each use of that name in the copy is the constant.

`return scale(2, x)` calls that copy and adds `2`.

When the file is `main.tlc`, the symbol is `main.scale.spec.2`.

A different constant builds a different copy.

`comptime name []const u8` is that string in the copy.

An enumerator is a comptime argument on that same parameter.

`take(n, Order.acquire)` calls `take.spec.1` when `acquire` is `1`.

Each use of that name is `Order.acquire`.

A method and a generic function drop that parameter the same way.

When the file is `main.tlc` and `acquire` is `1`, `p.id(Order.acquire)` calls `main.Point.id.spec.1`, and `id[i32](1, Order.acquire)` calls `main.id.spec.i32.spec.1`.

Each use of that name is `Order.acquire`.

`comptime if` on that compare keeps one arm.

The other arm is not typechecked and not emitted.

An ordinary `if` typechecks both arms.

A `var` of the enum is `type mismatch`.

A function nested in another function does not take an enumerator.

A `comptime const` whose initializer folds to `i32` is the same specialization as that integer.

A `const` and a `var` are `type mismatch`.

A function nested in another function still passes the constant.

`var f = scale` stores the address of `scale`. `f(2, x)` passes `2` and `x`. `scale(2, x)` still calls the specialized copy and does not pass `2`. `scale(n, x)` when `n` is a `var` is `type mismatch`. A body that does not typecheck while `n` is still a parameter is a name. `var f = fill` then `f(4, 38)` calls the specialized copy. `call(fill, 38)` calls `main.call.spec.fnfill`, and that copy calls `main.fill.spec.4`.

More than 32 nested specializations are `not supported yet`.

A struct, a float, an alias, a function, and a tuple are comptime arguments.

`comptime` is a keyword and sits directly before the argument name. `func add(n comptime i32)` and `func Point::shift(self *this, n comptime i32)` are `expected a type`.

```c
func add(comptime n i32) i32 {
    return n;
}

func scale(comptime n i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    var x = 40;
    return add(1) + scale(2, x);
}
```

```asm
scale:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

`return add(1)` folds to `1`.

`return scale(2, x)` passes `x` and calls `scale`, and `2` is an immediate.

## A comptime integer argument

A `comptime const` whose initializer folds to `i32` is that same argument.

`comptime const n = 2` then `return scale(n, x)` calls `main.scale.spec.2` when the file is `main.tlc`.

A runtime `const` and a `var` are `type mismatch`.

`scale(n + 1, x)` is `type mismatch`.

```c
func scale(comptime n i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    comptime const n = 2;
    var x = 40;
    return scale(n, x);
}
```

```asm
scale.spec.2:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq scale.spec.2               # call scale
retq                             # return
```

`n` is not stored.

The call passes `x` and adds `2`.

## A struct on a comptime function

A `comptime const` of a struct is that argument.

The literal is the same value.

`comptime const origin = Point{.x = 40, .y = 2}` then `return field(origin)` folds.

`comptime const n = origin.x` is that field. `assert(origin.x == 40, "x")` folds. `comptime if (origin.x == 40)` and `comptime switch (origin.x)` keep that arm.

`x i32 = 40` is that value when the literal does not name the field. `comptime const p = Point{}` then `p.x` is 40. `Point{.x = 40}` then `p.y` is the value written on `y`. `Point{0...}` stores 0.

`assert(row[0].x == 40, "x")` folds when `row` is a `comptime const` array of those structs.

A `var` does not fold. `assert(p.x == 40, "x")` on `var p Point` is `not supported yet`. A `const` that is not `comptime` is `not supported yet` as that field and as an argument.

That `field` is `comptime func field(p Point) i32 { return p.x + p.y; }`.

There is no runtime symbol.

```c
type Point struct {
    x i32;
    y i32;
};

comptime const origin = Point{.x = 40, .y = 2};

comptime func field(p Point) i32 {
    return p.x + p.y;
}

func main() i32 {
    return field(origin);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`field(Point{.x = 1, .y = 2})` folds the same way.

An enumerator passed to a `comptime func` folds when the body returns a constant `i32`.

`return rank(Order.acquire)` is `movl $42, %eax` and `retq`.

`type Integer i32` then `return id(Integer(4))` is `movl $4, %eax` and `retq`.

That `id` is `comptime func id(n Integer) i32 { return i32(n); }`.

A file-scope `const origin = Point{.x = 40, .y = 2}` then `field(origin)` is 42. The call is not emitted. `main` is `movl $42, %eax` and `retq`. `func take(comptime p Point, n i32)` takes that same `origin`. `take(origin, 40)` is 42.

`const k = 40` then `Point{.x = k, .y = 2}` is that struct. `const other = origin` is that value. `Point{0...}` stores 0 in each integer field. `comptime if (origin.x == 40)` keeps that arm. `const e = Order.equal` then `comptime if (e == Order.equal)` keeps that arm.

`const e = Order.equal` then `rank(e)` is that same pair of instructions when the body returns 42 for `Order.equal`.

`type Integer i32` then `const n = Integer(4)` and `id(n)` is `movl $4, %eax` and `retq`.

A local `const` of that struct, enumerator, or alias is `not supported yet`. A `var` is `var is not a compile-time constant` when it is the initializer of a `comptime const`.

## A struct returned from a comptime function

The result is the struct literal that call kept.

A file-scope `const` may be that call. `const p = origin()` stores the literal. `p.x` and `p.y` read those fields.

The body may be one `return` of a literal, or statements that build the value.

`origin` is `Point{.x = 41, .y = 1}`.

`move` returns `Point{.x = 42, .y = 1}`.

```c
type Point struct {
    x i32;
    y i32;
};

comptime const origin = Point{.x = 41, .y = 1};

comptime func move(p Point) Point {
    var copy = p;
    copy.x = copy.x + 1;
    return copy;
}

func main() i32 {
    return move(origin).x;
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

The call has no runtime symbol and no trace frame.

`return Point{.x = p.x + 1, .y = p.y}` is that same value.

`comptime const made = move(origin)` names that struct.

`made.x` is `42`.

A field may be an integer, a `bool`, an `f32`, an `f64`, an enumerator, a nested struct, an array of those, a `[]const u8`, a function, or `null`.

`made.flag` stays `bool`.

`made.flag + 1` is `type mismatch`.

`made.at.x` is the nested field.

`made.row[0]` is the element.

`made.label[0]` is the byte, and `made.label.len` is the length.

Those bytes are copied out of the run.

`made.op` is that function.

`(made.op)(4, 1)` calls it.

`null` stays `null`.

`width[made.x]()` is `main.width.spec.42` when the file is `main.tlc`.

That copy is `movl $42, %eax` and `retq`.

`comptime if (made.x == 42)` keeps one arm.

`comptime if (made.bias == 0.5)` keeps one arm.

When the file is `main.tlc`, `take(move(origin), n)` on `func take(comptime p Point, n i32)` calls `main.take.spec.p_42_1`.

`p.x` is the immediate `42`.

The struct parameter is not passed.

```asm
take.spec.p_42_1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $42, %eax                   # add
retq                             # return
```

`var made = move(origin)` copies that struct. `made.x` is 42. The call is not emitted.

```c
func main() i32 {
    var made = move(origin);
    return made.x;
}
```

```asm
main:
movl $42, -16(%rsp)              # made.x
movl $1, -12(%rsp)               # made.y
movl -16(%rsp), %eax             # made.x
movl -12(%rsp), %ecx             # made.y
movl %ecx, -4(%rsp)              # move
movl %eax, -8(%rsp)              # move
movl -8(%rsp), %eax              # made.x
retq
```

The program returns 42.

A field or a tuple element may be `3000000000u32`. That value keeps the suffix. The same bits passed to a comptime `i32`, or written as `3000000000i32`, are `value does not fit`.

A data pointer field is `global initializer is not a constant`.

A union result is `global initializer is not a constant`.

## A comptime method

A method is `comptime func Point::twice(self this) i32`.

The receiver is a `comptime const`, a `comptime` parameter, `typeof`, or `name := expr`. A type is known while compiling, so `typeof` is that value.

A `var` or a `const` is `not supported yet`.

`extern` and `export` on that method are `not supported yet`.

```c
type Point struct { x i32; };

comptime func Point::twice(self this) i32 {
    return self.x * 2;
}

func main() i32 {
    comptime const p = Point{.x = 21};
    return p.twice();
}
```

```asm
main:
subq $40, %rsp
movl $21, 24(%rsp)               # p.x
movl 24(%rsp), %eax              # move
movl %eax, 32(%rsp)              # move
movl 32(%rsp), %eax              # move
movl %eax, 12(%rsp)              # move
# trace frame for the call, then the matching pop
movl 12(%rsp), %edi              # move
callq main.Point.twice           # call Point.twice
movl 8(%rsp), %eax               # move
addq $40, %rsp
retq                             # return

main.Point.twice:
movl %edi, -8(%rsp)              # self.x
movl -8(%rsp), %eax              # move
shll %eax                        # shift
retq                             # return
```

`self.x` is loaded and shifted left by one.

The program returns 42.

```c
func main() i32 {
    var p = Point{.x = 21};
    return p.twice();
}
```

That program emits nothing.

`extern` and `export` on a `comptime func` are `not supported yet`.

## A tuple on a comptime function

A tuple of comptime values folds the same way.

`return field((40, 2))` has no runtime symbol.

```c
comptime func field(p (i32, i32)) i32 {
    return p[0] + p[1];
}

func main() i32 {
    return field((40, 2));
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`p[0]` is `40` and `p[1]` is `2`.

A `var` or a `const` passed to that function is `not supported yet`.

```c
func main() i32 {
    var pair = (40, 2);
    return field(pair);
}
```

That program emits nothing.

## A specialized struct parameter

A mix of a struct parameter and a runtime parameter stores the literal in the copy and drops the parameter.

When the file is `main.tlc`, `take(Point{.x = 2, .y = 0}, n)` calls `main.take.spec.p_2_0`.

`comptime const origin = Point{.x = 2, .y = 0}` calls that same copy.

A `const` in that position is `type mismatch`.

The fields are named in source order.

`Point{.y = 2, .x = 1}` calls `main.take.spec.p_2_1`.

`here.shift(Point{.x = 2, .y = 0})` calls `main.Point.shift.spec.p_2_0`.

That copy loads `self.x` and adds the stored `x` field.

A local `const` of a struct, a float, an alias, or a function is `type mismatch`.

A parameter `[2]i32` is a pointer.

`const row = [_]i32{1, 2}` is `type mismatch: expected *i32, found [2]i32`.

A function nested in another function passes the struct literal.

It does not take an enumerator.

```c
type Point struct {
    x i32;
    y i32;
};

func take(comptime p Point, n i32) i32 {
    return p.x + n;
}

func main() i32 {
    var n = 40;
    return take(Point{.x = 2, .y = 0}, n);
}
```

```asm
take.spec.p_2_0:
movl %edi, -12(%rsp)             # move
movl $2, -8(%rsp)                # constant
movl $0, -4(%rsp)                # constant
movl -8(%rsp), %eax              # move
movl -4(%rsp), %ecx              # move
addl -12(%rsp), %eax             # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.p_2_0       # call take
retq                             # return
```

`n` is stored.

The literal stores `x` as `2` and `y` as `0`.

The copy then loads both.

The add is `x + n`.

The call passes `n` and does not pass the struct.

## A specialized tuple parameter

A tuple is that argument too, and the copy does not store the tuple.

`p[0]` of `(2, 0)` is the immediate `2`.

When the file is `main.tlc`, `take((2, 0), n)` calls `main.take.spec.t_2_0`.

`here.shift((2, 0))` calls `main.Point.shift.spec.t_2_0` and loads `self.x` before adding `2`.

`id[i32](40, (2, 0))` calls `main.id.spec.i32.spec.t_2_0`.

A string element uses the spelling of a comptime string, so `("hi", 2)` calls `main.take.spec.t_s26869_2`.

When the tuple is the only parameter and the body returns `p[0]`, `return take((42, 0))` is `movl $42, %eax` and `retq`.

The compiler still compiles that function.

A `var` tuple is `type mismatch`.

A local `const` of a tuple is `type mismatch`.

A tuple element that is a local runtime `const` is `type mismatch`.

An element that is a `var` is `type mismatch`.

### A bool element

A `bool` element stays `bool`.

When the file is `main.tlc`, `take((true, 2), n)` calls `main.take.spec.t_b1_2`.

`p[0]` is `true`, `i32(p[0])` is `1`, and `false` is `b0`.

`p[0] + 1` is `type mismatch`.

### A named tuple

`comptime const pair = (40, 2)` names that tuple, and `pair[0]` is that element.

`comptime const flags = (true, 1)` names that tuple, and `flags[0]` stays `bool`.

`if (flags[0])` and `flags[0] == true` use that value.

`flags[0] + 1` is `type mismatch`.

An element may be a `bool`, an `f64`, an `f32`, a struct, or a `[]const u8`.

An element may also be an enumerator, a tuple, a function, a pointer, or an array.

A runtime `const` of a tuple is `type mismatch`.

### A nested tuple

A tuple may be an element.

`comptime const pair = ((40, 2), 1)` names that tuple, `pair[0]` is `(40, 2)`, and `pair[0][1]` is `2`.

When the file is `main.tlc`, `take(((40, 2), 1), n)` calls `main.take.spec.t_t_40_2_1`.

The compiler does not pass the parameter, and `p[0][1]` is the immediate `2`.

`((true, 2), 1)` is `t_t_b1_2_1`, and `p[0][0]` stays `bool`.

`p[0][0] + 1` is `type mismatch`.

`null` and an element that is not constant are `type mismatch`.

`func id[T type](value T)` of `((40, 2), 1)` calls `main.id.spec.tuple.2.tuple.2.i32.i32.i32.spec.t_t_40_2_1` and does not pass the tuple.

A function nested in another function passes the tuple and does not specialize.

```c
func take(comptime p (i32, i32), n i32) i32 {
    return p[0] + n;
}

func main() i32 {
    var n = 40;
    return take((2, 0), n);
}
```

```asm
take.spec.t_2_0:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.t_2_0       # call take
retq                             # return
```

`n` is stored and increased by `2`.

The call passes `n` and does not pass the tuple.

## A function stored in a tuple

A function is that element.

`comptime const ops = (add, 1)` names that tuple.

The element is a function pointer, the same type as `comptime const op = add`.

`(ops[0])(40, 2)` calls `add`.

`ops[0](40, 2)` is a generic call of `ops`.

`(ops[0][0])(40, 2)` calls a function in a nested tuple.

When the file is `main.tlc`, `take((add, 1), n)` calls `main.take.spec.t_fnadd_1`.

`take(ops, n)` is that copy.

`comptime const op = add` then `(op, 1)` is `t_fnadd_1`.

`lone(ops[0], n)` calls `main.lone.spec.fnadd`, the same spelling as `take(add, n)`.

`((add, 1), 2)` is `t_t_fnadd_1_2`.

The compiler does not pass the parameter, and the copy calls `main.add`.

A `var` of that tuple is `type mismatch`.

A parameter that is not `comptime` is `type mismatch`.

`null` is `type mismatch`.

A `var` is not promoted into a `comptime const`. `var f = add` then `comptime const ops = (f, 1)` is `var is not a compile-time constant`.

A `comptime func` in that `var` is that error. Write `comptime const f = add`.

A function element is called through that pointer.

`(p[0])(n, 2)` is the call, and `p[0](n, 2)` is a generic call of `p`.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func take(comptime p (func (i32, i32) i32, i32), n i32) i32 {
    return (p[0])(n, 2) + p[1];
}

func main() i32 {
    var n = 39;
    return take((add, 1), n);
}
```

```asm
add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

take.spec.t_fnadd_1:
movl %edi, 20(%rsp)              # move
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
movl $2, %esi                    # constant
callq main.add                   # call add
addl $1, %eax                    # add
retq                             # return

main:
movl $39, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.t_fnadd_1   # call take
retq                             # return
```

`n` is stored.

The copy calls `main.add` with `n` and `2`, then adds `1`.

The call passes `n` and does not pass the tuple.

## A pointer stored in a tuple

A pointer is that element when it is `&` of a `comptime const`.

`comptime const pair = (&addr, 2)` names that tuple, and `*pair[0]` is `addr`.

`comptime const p = &addr` then `(p, 2)` is the same pointer.

`pair[0].x` reads a field when the pointer addresses a struct.

`((&addr, 1), 2)` then `*nested[0][0]` is `addr`.

A local `comptime const` of that tuple is the same.

`comptime const n = *pair[0]` folds.

`return n + pair[1]` is `movl $42, %eax` and `retq` when `addr` is `40` and the other element is `2`.

A `comptime func` whose body is `return *value[0] + value[1]` folds the same way.

A `var` address, `comptime const pair = (&n, 2)`, is `var is not a compile-time constant`.

`null` is `type mismatch`.

Passing the tuple to a `comptime` parameter is `not supported yet`.

There is no spelling for the pointer.

`&addr` alone on `comptime p *i32` is `type mismatch`.

A pointer element is `&` of a `comptime const`.

`*pair[0]` is that integer.

`comptime const n = *pair[0]` folds it.

```c
comptime const addr = 40;
comptime const pair = (&addr, 2);
comptime const n = *pair[0];

func main() i32 {
    return n + pair[1];
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`pair[0].x` reads a field when the pointer addresses a struct.

A `var` address is `var is not a compile-time constant`.

Passing `(&addr, 2)` to a `comptime` parameter is `not supported yet`.

`&addr` alone on `comptime p *i32` is `type mismatch`.

## An array stored in a tuple

An array is that element.

`comptime const row = [_]i32{40, 2}` then `comptime const pair = (row, 0)` names that tuple.

The element stays `[2]i32`.

`pair[0][0]` is `40` and `pair[0][1]` is `2`.

`([_]i32{40, 2}, 0)` is the same tuple.

`((row, 1), 0)` then `nested[0][0][0]` is `40`.

A local `comptime const` of that tuple is the same.

`comptime const n = pair[0][0]` folds.

`return n + pair[0][1]` is `movl $42, %eax` and `retq`.

A `comptime func` whose body is `return value[0][0] + value[0][1]` folds the same way.

A file-scope `const row = [_]i32{2, 0}` then `return field((row, 40))` is 42. The call is not emitted.

That `field` is `comptime func field(p ([2]i32, i32)) i32 { return p[0][0] + p[1]; }`.

`main` is `movl $42, %eax` and `retq`.

A local `const` of that array is `not supported yet`.

A `var` array in that tuple is `var is not a compile-time constant`.

`type Point struct { x i32; y i32; }` then `const row = [_]Point{Point{.x = 2, .y = 0}, Point{.x = 40, .y = 0}}` then `return field((row, 0))` is `movl $42, %eax` and `retq`.

That `field` is `comptime func field(p ([2]Point, i32)) i32 { return p[0][0].x + p[0][1].x; }`.

`comptime const row` of that array then `return field((row, 0))` is `movl $42, %eax` and `retq`.

A local `const` of that array is `not supported yet`. A `var` of that array is `var is not a compile-time constant`.

`take((row, 0), n)` with `comptime p ([2]Point, i32)` is `type mismatch`.

`pair[0][0] = 1` on that comptime const is `type mismatch`.

Passing the tuple to a `comptime` parameter is `not supported yet`.

There is no spelling for the array.

An array element stays `[2]i32`.

`pair[0][0]` is that integer.

`comptime const n = pair[0][0]` folds it.

```c
comptime const row = [_]i32{40, 2};
comptime const pair = (row, 0);
comptime const n = pair[0][0];

func main() i32 {
    return n + pair[0][1];
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`return pair[0][0] + pair[0][1]` is those same two instructions.

A `comptime func` whose body is `return value[0][0] + value[0][1]` folds the same way when the call is `use(pair)`.

### A nested array

A second index reads a nested array.

`pair[0][1][0]` is element 0 of row 1.

The sum below is 42, so `main` is that constant.

```c
comptime const grid = [_][2]i32{{40, 1}, {2, 3}};
comptime const pair = (grid, 0);
comptime const n = pair[0][1][0];

func main() i32 {
    return n + pair[0][0][0];
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`const grid = [_][2]i32{{2, 0}, {40, 0}}` then `return field((grid, 0))` folds the same way.

That `field` is `comptime func field(p ([2][2]i32, i32)) i32 { return p[0][0][0] + p[0][1][0]; }`.

`main` is `movl $42, %eax` and `retq`.

A local `const` of that array is `not supported yet`. A `var` of that array is `var is not a compile-time constant`.

A `comptime func` folds the same indexes, and `pick(pair)` is `2 + 1`.

```c
comptime const grid = [_][2]i32{{40, 1}, {2, 3}};
comptime const pair = (grid, 0);

comptime func pick(value ([2][2]i32, i32)) i32 {
    return value[0][1][0] + value[0][0][1];
}

func main() i32 {
    return pick(pair);
}
```

```asm
main:
movl $3, %eax                    # constant
retq                             # return
```

### A runtime copy of the array

`var pair = (row, 0)` copies the array into the tuple.

`pair[0][0] = 1` stores in that copy, `row[0]` stays 40, and the sum is 41.

```c
comptime const row = [_]i32{40, 1};

func main() i32 {
    var pair = (row, 0);
    pair[0][0] = 1;
    return row[0] + pair[0][0];
}
```

```asm
main:
movl main.row(%rip), %eax        # row[0]
movl main.row+4(%rip), %ecx      # row[1]
movl %ecx, -12(%rsp)             # copy of row[1]
movl %eax, -16(%rsp)             # copy of row[0]
movl $0, -8(%rsp)                # pair's second element
movl $1, -16(%rsp)               # pair[0][0] = 1
movl main.row(%rip), %eax        # row[0]
addl -16(%rsp), %eax             # row[0] + pair[0][0]
retq                             # return
```

`var nested = ((row, 1), 0)` keeps the array in that copy.

`nested[0][0][0] = 2` stores there, `row[0]` stays 40, and the sum is 42.

```c
comptime const row = [_]i32{40, 1};

func main() i32 {
    var nested = ((row, 1), 0);
    nested[0][0][0] = 2;
    return row[0] + nested[0][0][0];
}
```

```asm
main:
movl main.row(%rip), %eax        # row[0]
movl main.row+4(%rip), %ecx      # row[1]
movl %ecx, -12(%rsp)             # copy of row[1]
movl %eax, -16(%rsp)             # copy of row[0]
movl $0, -4(%rsp)                # outer element
movl $1, -8(%rsp)                # inner element
movl $2, -16(%rsp)               # nested[0][0][0] = 2
movl main.row(%rip), %eax        # row[0]
addl -16(%rsp), %eax             # row[0] + nested[0][0][0]
retq                             # return
```

> **Warning:** A `var` array still loads.
>
> `comptime const n = row[0]` is 40. `assert(row[1] == 2, "x")` folds when `row` is `[_]i32{40, 2}`. `assert(row[0].x == 40, "x")` folds when `row` is a `comptime const` array of structs.
>
> An index outside that array is `index out of range`. A lone index of a `var` array still loads.
>
> `var row = [_]i32{40, 2}` then `comptime const pair = (row, 0)` is `var is not a compile-time constant`.
>
> `take(pair, 0)` with `comptime p ([2]i32, i32)` is `not supported yet`.
>
> There is no spelling for the array.
>
> `pair[0][0] = 1` on a `comptime const pair` is `type mismatch`.

## A float argument

A float is that argument too.

`comptime if (v == 0.5)` keeps one arm.

When the file is `main.tlc`, `const scale = 0.5` and the literal `0.5` call `main.take.spec.d3fe0000000000000`.

The other arm is not emitted.

```c
const scale = 0.5;

func take(comptime v f64, n i32) i32 {
    comptime if (v == 0.5) {
        return n + 2;
    }
    return n;
}

func main() i32 {
    var n = 40;
    return take(scale, n);
}
```

```asm
take.spec.d3fe0000000000000:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.d3fe0000000000000 # call take
retq                             # return
```

`n` is stored and increased by `2`.

The call passes `n` and does not pass `scale`.

`const k = 0.5` passed to a `comptime func` folds. The call is not emitted.

```c
const k = 0.5;

comptime func field(p f64) i32 {
    if (p == 0.5) {
        return 42;
    }
    return 0;
}

func main() i32 {
    return field(k);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

`const k = 0.5f` and `const flag = true` are that call. `main` is those two instructions. A local `const` is `not supported yet`.

`const k = 3000000000u32` then `field(k)` is those two instructions. `const k = 3000000000` against `i64` and `const k = 4u64` against `u64` are that call. `40u32` and `40i64` are too. `const a = 40i8` against `i8` is that call. `200u8`, `40i16`, and `40000u16` are too. `(k, 42)` passes that value in the tuple. A local `const` is `not supported yet`. `const k = 200i8` is `value does not fit`.

`const n = 4i8` then `var row [n]i32` has `row.len` of 4. `main` is `movl $4, %eax` and `retq`. `4u8`, `4i16`, and `4u16` are that length. `width[4i8]()` and `width[n]()` are `type mismatch`.

`const n = 4i8` then `comptime if (n == 4i8)` keeps that arm. `comptime switch (n)` with `case 4i8` keeps it too. `200u8`, `40i16`, and `4u64` are that value. `main` is `movl $42, %eax` and `retq`. A `var` is `not supported yet`.

```c
const k = 3000000000u32;

comptime func field(p u32) i32 {
    if (p == 3000000000u32) {
        return 42;
    }
    return 0;
}

func main() i32 {
    return field(k);
}
```

```asm
main:
movl $42, %eax                   # constant
retq                             # return
```

## An alias or a function argument

An alias is that argument too.

`type Integer i32` then `const other = Integer(4)` on `comptime n Integer` calls `main.take.spec.4` when the file is `main.tlc`.

The copy adds `4`.

`const other = Integer(4)` then `return field((other, 38))` folds.

That `field` is `comptime func field(p (Integer, i32)) i32 { return i32(p[0]) + p[1]; }`.

`main` is `movl $42, %eax` and `retq`.

`take((other, 0), n)` calls `main.take.spec.t_4_0`.

A local `const` in that tuple is `not supported yet`. A `var` in that tuple is `var is not a compile-time constant`.

A function is that argument too.

`take(add, n)`, `const op = add`, `BinOp(add)`, and `const wrapped = BinOp(add)` call `main.take.spec.fnadd`.

That copy calls `main.add` with `n` and `2`.

## A comptime function as that argument

A `comptime func` is that argument too.

`take(add, n)` and `comptime const op = add` call `main.take.spec.fnadd` when the file is `main.tlc`.

That copy calls `main.add` with `n` and `2`.

The copy does not paste the body.

A direct call `add(40, 2)` still folds, with no trace frame.

`BinOp(add)`, `(add, 1)`, `var f = add`, and `const op = add` are values too.

```c
comptime func add(a i32, b i32) i32 {
    return a + b;
}

comptime const op = add;

func take(comptime op func (i32, i32) i32, n i32) i32 {
    return op(n, 2);
}

func main() i32 {
    var n = 40;
    return take(add, n) + take(op, n) - 42;
}
```

```asm
main.add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return

main.take.spec.fnadd:
movl %edi, 20(%rsp)              # move
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
movl $2, %esi                    # constant
callq main.add                   # call add
retq                             # return

main:
movl $40, 36(%rsp)               # constant
movl 36(%rsp), %eax              # move
movl %eax, 24(%rsp)              # move
# trace frame for the call, then the matching pop
movl 24(%rsp), %edi              # move
callq main.take.spec.fnadd       # call take
movl 36(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq main.take.spec.fnadd       # call take
movl 4(%rsp), %ecx               # move
movl 20(%rsp), %eax              # move
addl %ecx, %eax                  # add
subl $42, %eax                   # subtract
retq                             # return
```

`n` is stored.

Each call passes `n` and does not pass `add`.

The copy calls `main.add` with `n` and `2`.

The program returns 42.

`BinOp(add)` and `comptime const wrapped = BinOp(add)` call `main.take.spec.fnadd`.

`(add, 1)` and `comptime const pair = (add, 1)` call `main.take.spec.t_fnadd_1`.

That copy calls `main.add` with `n` and `2`, then adds `1`.

`var f = add` and `const op = add` store the address of `main.add`.

`f(1, 0)` and `op(1, 0)` call through that pointer.

`add(1, 0)` still folds. The call is `addl $1`.

A `var` is not promoted into a `comptime const`. `var f = add` then `comptime const named = f` is `var is not a compile-time constant`.

`f` stays a `var`. Write `comptime const f = add`.

A file-scope `var held = add` then `comptime const op = held` is `global initializer is not a constant`.

`const saved = add` then `comptime const folded = saved` is the same copy.

`var f = scale`, when `scale` has a comptime parameter and a runtime parameter, stores the address of `scale`.

`f(2, x)` calls through that pointer and passes both arguments.

`scale(2, x)` still calls `main.scale.spec.2` and does not pass `2`.

`scale(n, x)` when `n` is a `var` is `type mismatch`.

```c
func scale(comptime n i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    var x = 40;
    var f = scale;
    return scale(2, x) + f(2, x);
}
```

```asm
main.scale.spec.2:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
subq $56, %rsp                   # allocate
movl $40, 44(%rsp)               # x = 40
leaq main.scale(%rip), %rax      # address of scale
movq %rax, 48(%rsp)              # f = scale
movl 44(%rsp), %eax              # x
movl %eax, 32(%rsp)              # move
# trace frame for the call, then the matching pop
movl 32(%rsp), %edi              # x
callq main.scale.spec.2          # call scale(2, x)
movl %eax, %ecx                  # move
movl %ecx, 28(%rsp)              # move
movq 48(%rsp), %rax              # f
movq %rax, 8(%rsp)               # move
movl 44(%rsp), %eax              # x
movl %eax, 16(%rsp)              # move
# trace frame for the call, then the matching pop
movq 8(%rsp), %rax               # f
movl 16(%rsp), %esi              # x
movl $2, %edi                    # constant
callq *%rax                      # call f
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %ecx               # move
movl 28(%rsp), %eax              # move
addl %ecx, %eax                  # add
addq $56, %rsp                   # release
retq                             # return

main.scale:
movl %edi, -8(%rsp)              # n
movl %esi, -4(%rsp)              # x
movl -4(%rsp), %eax              # x
addl -8(%rsp), %eax              # add n
retq                             # return
```

`scale(2, x)` adds `2`.

`f(2, x)` adds the two arguments of `scale`.

The program returns 84.

`const f = scale` stores that same address.

A file-scope `var f = scale` stores it in `main.f` when the file is `main.tlc`.

`comptime if` on `n` stays in that function. Both arms are typechecked, and the condition runs at run time. `scale(2, x)` still keeps one arm and leaves the other unchecked.

A body that does not typecheck while `n` is still a parameter is a name. There is no function pointer.

`var f = fill` then `f(4, 38)` calls `main.fill.spec.4` when the file is `main.tlc`. `4` is not passed. `out.len` is `4`.

The program returns 42.

`fill(4, value)` calls that same copy.

`call(fill, 38)` calls `main.call.spec.fnfill` when the file is `main.tlc`.

That copy calls `main.fill.spec.4`. `4` is not passed. `38` is.

`var f = fill` then `call(f, 38)` is that copy.

The program returns 42.

`f(n, value)` when `n` is a `var` is `type mismatch`.

```c
func fill(comptime n i32, value i32) i32 {
    var out [n]i32;
    return out.len + value;
}

func main() i32 {
    var f = fill;
    return f(4, 38);
}
```

```asm
main.fill.spec.4:
movl %edi, -20(%rsp)             # value
movl -20(%rsp), %eax             # value
addl $4, %eax                    # out.len + value
retq                             # return

main:
subq $24, %rsp                   # allocate
# trace frame for the call, then the matching pop
movl $38, %edi                   # constant
callq main.fill.spec.4           # call f(4, 38)
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # return
addq $24, %rsp                   # release
retq                             # return
```

```c
func fill(comptime n i32, value i32) i32 {
    var out [n]i32;
    return out.len + value;
}

func call(op func (i32, i32) i32, value i32) i32 {
    return op(4, value);
}

func main() i32 {
    return call(fill, 38);
}
```

```asm
main.fill.spec.4:
movl %edi, -20(%rsp)             # value
movl -20(%rsp), %eax             # value
addl $4, %eax                    # out.len + value
retq                             # return

main.call.spec.fnfill:
subq $24, %rsp                   # allocate
movl %edi, 20(%rsp)              # value
movl 20(%rsp), %eax              # value
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # value
callq main.fill.spec.4           # call fill(4, value)
movl %eax, %ecx                  # move
movl %ecx, 4(%rsp)               # move
movl 4(%rsp), %eax               # return
addq $24, %rsp                   # release
retq                             # return

main:
subq $24, %rsp                   # allocate
# trace frame for the call, then the matching pop
movl $38, %edi                   # constant
callq main.call.spec.fnfill      # call call(fill, 38)
movl %eax, %ecx                  # move
movl %ecx, 12(%rsp)              # move
movl 12(%rsp), %eax              # return
addq $24, %rsp                   # release
retq                             # return
```

`38` is stored.

The copy calls `main.fill.spec.4` with `38`, then returns `42`.

A `var` is not promoted into a `comptime const`. An ordinary function stored in a `var`, then `comptime const op = f`, is `var is not a compile-time constant`.

```c
comptime func add(a i32, b i32) i32 {
    return a + b;
}

type BinOp func (i32, i32) i32;

func take(comptime op func (i32, i32) i32, n i32) i32 {
    return op(n, 2);
}

func main() i32 {
    var n = 40;
    var f = add;
    const op = add;
    return take(BinOp(add), n) + f(1, 0) + op(1, 0) + add(1, 0) - 3;
}
```

```asm
main:
movl $40, 84(%rsp)               # constant
leaq main.add(%rip), %rax        # address
movq %rax, 88(%rsp)              # move
movq %rax, 96(%rsp)              # move
movl 84(%rsp), %eax              # move
movl %eax, 72(%rsp)              # move
# trace frame for the call, then the matching pop
movl 72(%rsp), %edi              # move
callq main.take.spec.fnadd       # call take
movl %eax, %ecx                  # move
movl %ecx, 68(%rsp)              # move
movq 88(%rsp), %rax              # move
movq %rax, 48(%rsp)              # move
# trace frame for the call, then the matching pop
movq 48(%rsp), %rax              # move
movl $1, %edi                    # constant
xorl %esi, %esi                  # constant
callq *%rax                      # call f
movl %eax, %ecx                  # move
movl %ecx, 44(%rsp)              # move
movl 68(%rsp), %eax              # move
addl %ecx, %eax                  # add
movl %eax, 20(%rsp)              # move
movq 96(%rsp), %rax              # move
movq %rax, 24(%rsp)              # move
# trace frame for the call, then the matching pop
movq 24(%rsp), %rax              # move
movl $1, %edi                    # constant
xorl %esi, %esi                  # constant
callq *%rax                      # call op
movl %eax, %ecx                  # move
movl %ecx, 16(%rsp)              # move
movl 20(%rsp), %eax              # move
addl %ecx, %eax                  # add
addl $1, %eax                    # add
subl $3, %eax                    # subtract
retq                             # return
```

`n` is stored.

`f` and `op` each hold `main.add`.

The direct call is the `addl $1`.

The program returns 42.

## An enumerator on an ordinary function

An enumerator is a comptime argument of that function.

The copy does not receive it.

The program returns 42.

When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.take.spec.1`.

`comptime if (order == Order.acquire)` keeps `return n + 2` and leaves the other arm out.

The other arm is not typechecked.

A method and a generic function do the same.

An ordinary `if` typechecks both arms.

```c
type Order enum {
    relaxed,
    acquire,
};

func take(n i32, comptime order Order) i32 {
    comptime if (order == Order.acquire) {
        return n + 2;
    }
    return n;
}

func main() i32 {
    var n = 40;
    return take(n, Order.acquire);
}
```

```asm
take.spec.1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $2, %eax                    # add
retq                             # return

main:
movl $40, 20(%rsp)               # constant
movl 20(%rsp), %eax              # move
movl %eax, 8(%rsp)               # move
# trace frame for the call, then the matching pop
movl 8(%rsp), %edi               # move
callq take.spec.1                # call take
retq                             # return
```

`n` is stored, loaded, and increased by `2`.

The other arm is not emitted.

The call passes `n` and does not pass `order`.

## An enumerator on a method

A method drops the parameter the same way.

The program returns 42.

When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.Point.id.spec.1`.

`comptime if (order == Order.acquire)` keeps `return self.x + 41`.

```c
type Order enum {
    relaxed,
    acquire,
};

type Point struct {
    x i32;
};

func Point::id(self *this, comptime order Order) i32 {
    comptime if (order == Order.acquire) {
        return self.x + 41;
    }
    return self.x;
}

func main() i32 {
    var p Point;
    p.x = 1;
    return p.id(Order.acquire);
}
```

```asm
main.Point.id.spec.1:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
addl $41, %eax                   # add
retq                             # return

main:
movl $1, 16(%rsp)                # constant
# trace frame for the call, then the matching pop
leaq 16(%rsp), %rdi              # move
callq main.Point.id.spec.1       # call Point.id
retq                             # return
```

`self.x` is loaded and increased by `41`.

The other arm is not emitted.

The call passes `p` and does not pass `order`.

## An enumerator on a generic function

A generic function drops the parameter after the type arguments.

The program returns 42.

When the file is `main.tlc` and `acquire` is `1`, the symbol is `main.id.spec.i32.spec.1`.

The copy keeps `return value + 41`.

```c
type Order enum {
    relaxed,
    acquire,
};

func id[T type](value T, comptime order Order) T {
    comptime if (order == Order.acquire) {
        return value + 41;
    }
    return value;
}

func main() i32 {
    return id[i32](1, Order.acquire);
}
```

```asm
main.id.spec.i32.spec.1:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
addl $41, %eax                   # add
retq                             # return

main:
# trace frame for the call, then the matching pop
movl $1, %edi                    # constant
callq main.id.spec.i32.spec.1    # call id
retq                             # return
```

`value` is loaded and increased by `41`.

The call passes `1` and does not pass `order`.

## Extern does not keep an unspecialized function

A bare `extern` and `extern(abi="tlc")` on that function are `not supported yet`.

```c
extern(abi="tlc") func scale(comptime n i32, x i32) i32 {
    return x + n;
}

func main() i32 {
    return 0;
}
```

That declaration emits nothing.

`extern(abi="c")` passes the constant and is not specialized.

## A compile-time string

`comptime const text = "hi"` is a comptime string.

`len(text)` folds to `2`.

That `len` is `comptime func len(text []const u8) i32 { return text.len; }`.

A `var` or a `const` passed to that function is `not supported yet`.

## A comptime overload

A function may declare the same name and the same number of arguments again, one list with `comptime` and one without.

A generic function may do the same on a type parameter.

A method may do the same after `self`.

The list with `comptime` is chosen when every `comptime` argument is a comptime value.

It is preferred over the list without `comptime`.

A `var` chooses the list without `comptime`.

`id(1)` calls the `comptime` list. `id(n)` when `n` is a `var` calls the other list.

When only the `comptime` list exists and an argument is not a comptime value, the call is `type mismatch`.

Two lists that both mark the same parameters `comptime` are `redefinition`.

[`mem.memeql`](../std/mem.md) and `mem.memcmp` fold two constant `[]const u8` values through that overload.

[Index](../README.md) · [Previous: Generics](generic.md) · [Next: Macros](macro.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
