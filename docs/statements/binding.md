# Bindings

[Index](../README.md) · [Previous: defer](defer.md) · [Next: new and delete](new.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A variable is `var name type`, `var name = expr`, or `name := expr`. `i32 value = 1` is reported as `expected var`. When `var` has an initializer, the type may be omitted and is taken from that value: `var value = 1`, `var p = &value`, `var p = Point{}`. The type can still be written after the name, with a pointer or an array in front of its element type: `var value i32 = 1`, `var p *i32`, `var buf [32]u8`, `var ps [2]*i32`. One `var` names one variable. A variable with no initializer keeps the type: `var value i32;`. A local with no initializer is uninitialized. Reading it before a store is reported as `uninitialized`. A file-scope variable with no initializer is zero. A local struct field takes the value written on the field. A field with no value stays uninitialized. A file-scope field with no value is zero. `0...` at the end of a brace list stores 0 in every value the list does not name. An array list may be shorter than the array. The elements it does not name stay uninitialized, unless the list ends with `0...`. At file scope, a shorter list and a list that ends with `0...` are reported as `global initializer is not a constant` when code is emitted. `--check` accepts that list. `const` is fully set by its initializer: every field and every element has a value, from the list, from `0...`, or from a value written on the field. A `*T` still needs an initializer. A `?*T` may be left unset.

With no `-O` flag, a store is removed when a later store replaces the value before a read, or when the value is not read. `-O0` keeps the store. There is no store for a local that was left uninitialized. `sizeof`, `&`, and `.len` of a sized array do not read the stored value. Passing a sized array to a parameter `[N]T` passes the address. That call does not load the elements and does not initialize them. A later read is `uninitialized`. `fill(&row)` when the parameter is `*[N]T` initializes the array. `*T(array)` is that address. A written `*T`, in `var p *T = array` or a parameter `*T`, is that same address and does not initialize the elements. `var v []T = array` is a slice of the array. Copying the array does: `var copy = row`, `var copy [N]T = row`, `copy = row`, and `return row` when the result is `[N]T`, reads every element. `var n i32 = n` reads `n` before the initializer stores it and is reported as `uninitialized`.

A `*T`, or a struct or array that contains one, needs an initializer. `var p *i32;` is reported as `type mismatch`. `var p ?*i32;` is uninitialized. `var p ?*i32 = null` is `null`. A local `const` needs an initializer. `const n = 1` and `const n i32 = 1` are that binding. `const n;` and `const n i32;` are reported as `expected '='`. Assigning the local const is reported as `type mismatch`. `extern var p *i32;` is not checked. `name := expr` defines a new name. A later `var` or `:=` in the same block hides the earlier binding for the rest of the block. `var n = 1` then `var n = n + 1`, `n := n + 1`, and `var n i32 = n + 1` read the previous `n`. The new binding may have another type. A parameter is hidden the same way. `n = n + 1` assigns that same binding. A second `const` in the block is `redefinition`. Two names in one `var` or `:=`, such as `a, a := expr`, are `redefinition`. A file-scope `var` of the same name is `redefinition`. `var name := expr` is reported as `type mismatch`. Hiding `var n = 1` with `var n = n + 1` and returning it is 6 instructions, the same count as `n = n + 1`. The new binding has its own slot. An inner block reads the earlier binding the same way. After `var n = 1`, `{ var n i32 = n + 1; return n; }` returns 2, and `var n = n + 1` and `n := n + 1` in that block do the same. `var i i32 = i + 10` in a `for` body reads the loop variable. `var n i32 = n` with no earlier `n` stays `uninitialized`. `var p = &s` is that address. A field of `s` that was never stored is `uninitialized` through `p`, and a later `var p = &s` is the same. A later `var p = new Point{.x = 4}` or `p := new Point{.x = 4}` is `uninitialized` on a field the list leaves out, the same as the first `new`.

`*[4]i32` is a pointer to an array. `arr[1]` reads and writes the same element as `(*arr)[1]`. `&` of an array has that pointer type. `**[4]i32` is a pointer to that pointer. `(**deep)[2]` and `(*deep)[2]` read the same element. `?*[4]i32` is that pointer or `null`. `var deep **[4]i32` with no initializer is reported as `type mismatch`. `var opt ?*[4]i32` may omit the initializer. Reading it is reported as `uninitialized`.

```c
func main() i32 {
    var value = 1;
    var p *i32 = &value;
    var buf [3]i32 = {10, 20, 12};
    extra := 2;
    var typed i32 = 3;
    return *p + buf[0] + extra + typed;
}
```

```asm
main:
movl $1, -36(%rsp)               # constant
leaq -36(%rsp), %rax             # address
movq %rax, -32(%rsp)             # move
movl $10, -20(%rsp)              # constant
movl $2, -8(%rsp)                # constant
movl $3, -4(%rsp)                # constant
movq -32(%rsp), %rax             # move
movl (%rax), %eax                # move
addl -20(%rsp), %eax             # add
addl -8(%rsp), %eax              # add
addl -4(%rsp), %eax              # add
retq                             # return
```

A local array takes a brace list with one value per element: `var row [3]i32 = {10, 20, 12}`. A different count is reported as `type mismatch`. `row[0]` reads an element. A local `var copy = row` and `var copy [2]i32 = row` copy that array. A later store into `row` does not change `copy`. A parameter `[2]i32` is a pointer. `var copy [2]i32 = row` copies those elements. A later store into the parameter does not change the copy. `var copy = row` stays `*i32`. A list element copies that array too. `var row = [_][2]i32{inner}` and `var grid [2][2]i32 = {a, b}` store those rows. A later store into the source does not change the copy. A different length is reported as `type mismatch`. `copy = row` copies that array. A later store into `row` does not change `copy`. `row = {4, 6}` stores that list. A short list stores only the named elements. Reading an omitted element is reported as `uninitialized`. A different length, a `const` array, and `[2]const i32` are reported as `type mismatch`. `kept = row` copies those elements when `kept` is a local `[2]i32` and `row` is a parameter. A later store into the parameter does not change `kept`. A file-scope `var copy = row`, `const copy = row`, and `var copy [2]i32 = row` copy that array into the binary.

`[_]i32{10, 20, 12}` is an array. `_` stands for the length, which is the number of values, so the type is `[3]i32`. `var row = [_]i32{10, 20, 12}`, `row := [_]i32{10, 20, 12}`, and a local `const row = [_]i32{10, 20, 12}` take that type. `var typed [2]i32 = [_]i32{4, 1}` matches the written length. A different length or a different element type is reported as `type mismatch`. Assigning an element of the const binding is reported as `type mismatch`. `row.len` is `i32`, the number of elements, the same spelling as a slice.

```c
func main() i32 {
    var row = [_]i32{10, 20, 12};
    other := [_]i32{7, 8};
    var typed [2]i32 = [_]i32{4, 1};
    return row[0] + row[2] + other[0] + other[1] + typed[0] + typed[1];
}
```

```asm
main:
movl $10, -28(%rsp)              # constant
movl $12, -20(%rsp)              # constant
movl $7, -16(%rsp)               # constant
movl $8, -12(%rsp)               # constant
movl $4, -8(%rsp)                # constant
movl $1, -4(%rsp)                # constant
movl -28(%rsp), %eax             # move
addl -20(%rsp), %eax             # add
addl -16(%rsp), %eax             # add
addl -12(%rsp), %eax             # add
addl -8(%rsp), %eax              # add
addl -4(%rsp), %eax              # add
retq                             # return
```

The same lists are file-scope initializers when every value is a constant. The element may be an integer, `bool`, a float, a pointer, a struct, an enum, a typedef, or a function pointer. `var named [3]i32 = {10, 4, 1}`, `var row = [_]i32{10, 20, 12}`, `other := [_]i32{6, 1}`, and `const fixed = [_]i32{19, 2}` are those arrays. `var live [2]i32 = [_]i32{1, 2}` matches the written length. A different count is reported as `type mismatch`. An empty `[_]i32{}` has length 0. A nested array is a brace list. `var grid = [_][2]i32{{1, 2}, {3, 4}}` reads `grid[0][1]`. `var row [2][3]i32 = {{1, 2, 3}, {4, 5, 6}}` and `var deep = [_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}` are the next array, and `deep[0][1][2]` reads an element. A global name copies that global's initializer, so `var n = 1` then `var row = [_]i32{n, 2}` keeps `1` in `row[0]` when `main` later assigns `n`. A sized array copies another array in this file the same way: `var copy = row`, `const copy = row`, `var copy [2]i32 = row`, `const frozen [2]i32 = held`, `var whole [2][2]i32 = grid`, and `var line [2]i32 = grid[1]`. A list element copies an array in this file the same way: `var row = [_][2]i32{inner}`, `[_][2]i32{grid[1]}`, and `var row [1][2]i32 = {late}` when `late` is declared later. `const inner = [_]i32{1, 2}` before that var is that copy. `var row = [_][2]i32{give()}` and `var row = [_][2]i32{unsafe give()}` run `give` before `main`. `var row = [_]i32{give(), 2}` and `var row [2]i32 = {give(), 2}` run `give` before `main` when `give` returns `i32`. Each element is stored before the next one runs, so that call reads the elements already stored. `[_]Point{give()}` stores that value. A `const` with that call, `[_]i32{n + 1}`, `[_]i32{give() + 1}`, and `var n = give(); var row = [_]i32{n}` are reported as `global initializer is not a constant`. `var copy [2]i32 = give()` and `var copy [2]i32 = unsafe give()` run `give` before `main`. Copying an array that is itself filled by a call is reported as `global initializer is not a constant`. A value that is not a constant is reported as `global initializer is not a constant`. A const array is copied by `const copy = held`. Assigning an element is reported as `type mismatch`. A pointer to the element is `*const i32(held)`, and `*i32(held)` is reported as `unsafe cast`. The name is not an array length. `&fixed` is reported as `not an lvalue`.

```c
const fixed = [_]i32{19, 2};
var named [3]i32 = {10, 4, 1};
other := [_]i32{6, 1};
var live [2]i32 = [_]i32{1, 2};

func main() i32 {
    live[0] = 3;
    return fixed[0] + fixed[1] + named[0] + named[2] + other[0] + other[1] + live[0];
}
```

```asm
main:
movl $3, (%rip)                  # constant
movl (%rip), %eax                # move
addl (%rip), %eax                # add
addl (%rip), %eax                # add
addl (%rip), %eax                # add
addl (%rip), %eax                # add
addl (%rip), %eax                # add
addl (%rip), %eax                # add
retq                             # return
```

The program returns 42.

```c
type Integer i32;

type Point struct {
    x i32;
    y i32;
};

type Color enum {
    Red,
    Blue,
};

type Entity struct {
    id i32;
};

func add(n i32) i32 {
    return n;
}

var n = 1;
var row = [_]i32{n, 2};
var grid = [_][2]i32{{1, 2}, {3, 4}};
var ops = [_]func (i32) i32{add};
var scales = [_]f32{1.0f};
var ptrs = [_]*i32{&n};
var origin = [_]Point{Point{.x = 10, .y = 1}};
var colors = [_]Color{Color.Blue};
var tags = [_]Integer{Integer(4)};
var empty = [_]i32{};
var Entity::bias = [_]i32{3, 1};

func main() i32 {
    n = 9;
    var f = ops[0];
    var q = ptrs[0];
    var local = [_][2]i32{{8, 0}};
    if (scales[0] != 1.0f || colors[0] != Color.Blue || sizeof(empty) != 0) {
        return 0;
    }
    return row[0] + row[1] + grid[0][1] + f(1) + *q + origin[0].x + origin[0].y + i32(tags[0]) +
        Entity.bias[0] + Entity.bias[1] + local[0][0];
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $1, %edi                    # constant
callq *%rax                      # call *%rax
retq                             # return

add:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %eax              # move
retq                             # return
```

The program returns 42. `row[0]` stays `1` after `n = 9`. `*q` reads the new value of `n`. `f` is a copy of `ops[0]`.

### `const`

`const name = expr` is a runtime variable that cannot be assigned. It has an address, at file scope and inside a function. `const name Type = expr` writes the type. Assigning the name is `type mismatch`. `&name` has type `*const T`.

`comptime const name = expr` is a compiler constant. The expression after `=` has to be a compile-time constant, the same kind `assert` folds: a literal, `sizeof`, float arithmetic, `==` or `!=` of two `typeof` values, an index of a constant `[]const u8`, another `comptime const`, or a call that folds. `"hi"[1]` is the byte 105. `"hello"[1:4][0]` is 101. A suffix whose value fits in `i32`, such as `1u32`, folds too. `comptime const N = 4` can be an array length, `[N]i32`. A plain `const` cannot. A `var` cannot. An initializer that is not a compile-time constant is `global initializer is not a constant` at file scope and `not supported yet` inside a function. `comptime const pair = (40, 2)` is that tuple. `pair[0]` is the element, and it can be an array length. An element may be a `bool`, an `f64`, an `f32`, a struct, a `[]const u8`, an enumerator, or a tuple, as in `comptime const wide = (0.5, 40)`, `comptime const origin = (Point{.x = 40, .y = 2}, 1)`, `comptime const label = ("hi", 2)`, `comptime const ink = (Color.Blue, 2)`, and `comptime const flags = (true, 1)`. `flags[0]` stays `bool`. `if (flags[0])` and `flags[0] == true` use that value. `flags[0] + 1` is `type mismatch`. `comptime const nested = ((40, 2), 1)` is that tuple. `nested[0]` is `(40, 2)`. `comptime const n = nested[0][1]` is `2`, and `[n]i32` uses it. `nested[0][0]` stays `bool` when that element is `true`. `nested[0][0] + 1` is `type mismatch`. `null` and an element that is not constant are `type mismatch`. A function pointer element is `not supported yet`. A file-scope `const` or `var` of a tuple is `type mismatch`.

A runtime `const` whose initializer is a compile-time constant can be promoted: `comptime const folded = name`. That is the explicit `comptime`. `comptime const sum = name + other` is `global initializer is not a constant` when `name` is only `const` and `other` is `comptime const`. Promote `name` first. `comptime const p = &addr` is a comptime pointer, and `addr` has to be a `comptime const`. `*p` is that value. An integer, a `bool`, an `f32`, an `f64`, and a struct fold the same way as `addr`. `&` of a runtime `const` or a `var` is not that pointer, and `*p` through it is not a compiler constant.

`f32` and `f64` can be that compiler constant too. `comptime const bias = 0.5f` is `f32`. `comptime const lifted f64 = bias` converts the width. `comptime const from_lit f32 = 1` converts a folded integer. The name is not an array length. Assigning it is `type mismatch`. `&bias` is `*const f32`. Copying a `var` into a `comptime const` float is `global initializer is not a constant`.

An integer or bool array can be that constant too: `const fixed = [_]i32{19, 2}` and `const flags = [_]bool{true, false}`. A float, a pointer, a struct, an enum, a typedef, or a function pointer can be the element when every value is a constant, as in `const origin = [_]Point{Point{.x = 1, .y = 2}}`. Copying a `var` into that array is reported as `global initializer is not a constant`. Reading the name copies the array. Assigning an element is reported as `type mismatch`. The name is not an array length, and it has no address. A struct, a function pointer, or a typedef can be that constant too. `const origin Point = {.x = 10, .y = 10}` and `const other = Integer(4)` are those types. A `comptime const` of a struct, a float, an alias, or a function is a comptime value. See [Comptime](../functions/comptime.md). A runtime `const` of those types is not, until `comptime const folded = name` promotes it. `const origin = Point{0...}` is a `Point` whose fields are 0. `const origin = Point{}` is `uninitialized` when a field has no value. `const origin = Point{.x = 10, .y = 10}` fills those fields. `const op BinOp = BinOp(add)` and `const plain func (i32, i32) i32 = add` name a function. The initializer has to be a constant. A call is reported as `global initializer is not a constant`. Assigning the constant, or a field of a struct constant, is reported as `type mismatch`. A method whose receiver is a non-const pointer is reported as `type mismatch: expected *Point, found *const Point`. `self *const this` can be called on the constant. It has no address. An alias is still a different type, so `var raw i32 = value` is reported as `type mismatch` when `value` is `Integer`. `i32(value)` confirms it. An alias is not an array length.

`var text = "hi"` and `const label = "hi"` are `[]const u8`. The length is 2 and does not count the terminating null. `*const u8` is the pointer a C library takes: `var text *const u8 = "hi"` and `const c_text *const u8 = "unix"`. A `[]const u8` converts to that pointer and drops the length. `const p *i32 = &n` stores that address when `n` is a variable. `comptime const p = &addr` requires `addr` to be a `comptime const`. Inside a function, `const text = "hi"` is the same slice.

`export const name = expr` and `export const name Type = expr` publish the constant. Another package reads it as `pkg.name` or `import { name }`. Assigning it is rejected the same way as any other constant. A constant without `export` is reported as `name is not exported`. `const Entity::next = expr` is a constant on that type, written beside the struct. See [Methods](../methods/constant.md).

Inside a function, `const name = expr` is that same runtime binding. `comptime const name = expr` is a compiler constant there too. A local `comptime const` whose value folds to `i32` is an array length, a comptime argument, and a `comptime if` condition. A runtime `const` is none of those, even when the initializer is a literal. Promote it with `comptime const folded = name`. A local `const` with no initializer is reported as `expected '='`. A store through `&name` is `type mismatch`.

`var name const Type` and `const Type name` are a variable whose value is constant. The variable has an address, the same as `const name = expr`. It is not a compiler constant. Assigning the name is `type mismatch`. `&name` has type `*const T`, and `var p *i32 = &name` is `type mismatch: expected *i32, found *const i32`. A copy of the value is not const, so `plain(n)` matches `func plain(n i32)` when `n` is `var n const i32`. That copy does not match a `comptime` parameter. `comptime const n = 2` does. A file-scope variable with no initializer is zero, the same as `var`. A local with no initializer is `uninitialized` when it is read. A parameter is `name const Type`, including `extern(abi="c")`. Assigning that parameter is `type mismatch`. The argument is still an `i32`. `type Name const Type` is an alias of that constant value. `type Count const i32` is a different type from `i32`. `Count(40)` and `i32(n)` confirm it. `var n Count = 40` is `type mismatch`. Assigning `n` is `type mismatch`.

`const Type name` is that variable when the token after `const` is a type the file already knows and the next token is the name. `i32`, `bool`, and a declared `Point` are those types. `const Vec[i32] v` is that variable when `Vec` is already a type. `const i32 = 1` is still a binding named `i32`, because `=` follows the type. `const n i32 = 1` and `const n = 1` are still the binding whose name comes first. `const Point p` before `type Point` is not this variable. `const int n` is `not supported yet`. `i32 n` is `expected var`. `for (const i32 i = 0; i < 1; i = i + 1)` is `type mismatch` on the increment. `const [4]i32 row`, `const n const i32`, `const *i32 p`, `func (const i32) i32`, and a result written `const i32` are `not supported yet`. `var row const [4]i32` is `expected a type`. `var Type::name const Type` is `not supported yet`. `type Name[T type] const T` is `expected ']'`. `[]const [4]T` is `not supported yet`. `*const T` and `kind const Type` are unchanged.

```c
func main() i32 {
    var n const i32 = 40;
    const i32 extra = 2;
    return n + extra;
}
```

```asm
main:
movl $40, -8(%rsp)               # constant
movl $2, -4(%rsp)                # constant
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

The program returns 42. `n` and `extra` are both stored. Assigning either name emits nothing.

```c
const N = 4;

func main() i32 {
    var buf [N]i32;
    const value = 40 + 2;
    var p *const i32 = &value;
    buf[0] = *p;
    return buf[0];
}
```

```asm
main:
movl $42, -12(%rsp)              # constant
leaq -12(%rsp), %rax             # address
movq %rax, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
movl %eax, -28(%rsp)             # move
movl -28(%rsp), %eax             # move
retq                             # return
```

```c
type Integer i32;

type Point struct {
    x i32;
    y i32;
};

func add(a i32, b i32) i32 {
    return a + b;
}

type BinOp func (i32, i32) i32;

const value Integer = Integer(20);
const other = Integer(2);
const origin Point = {.x = 10, .y = 8};
const op BinOp = BinOp(add);

func main() i32 {
    return i32(value) + i32(other) + origin.x + origin.y + op(1, 1);
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $1, %esi                    # constant
movl %esi, %edi                  # move
callq *%rax                      # call *%rax
retq                             # return

add:
movl %edi, -8(%rsp)              # move
movl %esi, -4(%rsp)              # move
movl -8(%rsp), %eax              # move
addl -4(%rsp), %eax              # add
retq                             # return
```

The program returns 42.

### File scope

At file scope, `var name = expr` and `name := expr` both infer the type from the initializer. `export` goes on either form: `export var bonus = 40` and `export extra := 2`. Another package reads and assigns `pkg.bonus` or `import { extra }`. `extern` and `extern(abi="c")` still go on a typed `var`: `extern var slot i32`, `extern(abi="c") var slot i32`. `extern` on `var name = expr` or `name := expr` is reported as `not supported yet`. `export` also goes on a file-scope `const` and on `var Type::name`. A global with no initializer is zero. A struct field takes the value written on the field, and is zero when the field has none. A float literal is a constant initializer. `var scale = 1.5f` is `f32` and `var wide = 20.0` is `f64`. `var copied = scale` keeps that initializer after `scale` is assigned. `var neg = -1.0` is the negated literal. `var added = 1.5f + 0.5f` folds that arithmetic. `var from_int f32 = n` converts a folded integer, and so does `var from_u f32 = 1u32`. `var spare f32` is zero. `var Entity::scale = 1.5f` is that variable on a type. A call used as the whole initializer of a `var` runs before `main`. `var item = call_func()`, `var item i32 = call_func()`, `item := call_func()`, and `var Entity::item = call_func()` are that form. `unsafe call_func()` is that call when the function is `unsafe func`. A function pointer works the same way: `var fp = call_func` then `var item = fp()`. `const item = call_func()` is reported as `global initializer is not a constant`. The same diagnostic is reported for `var n = call_func() + 1`, for a call inside an array or a struct literal, for `new`, and for `var copied = item` when `item` was initialized by a call. Calls in one file run in source order, so a later call reads an earlier variable that a call already stored. A constant global is already in the binary. An imported package runs before the file that imports it. Files in one package share that place and run in path order.

```c
var count = 1;
n := 2;
export var shown i32 = 4;
var hidden i32 = 3;

func main() i32 {
    return count + n + shown + hidden;
}
```

```asm
main:
movl (%rip), %eax                # move
addl (%rip), %eax                # add
movq (%rip), %rcx                # move
addl (%rcx), %eax                # add
addl (%rip), %eax                # add
retq                             # return
```

```c
extern var slot i32;
extern(abi="c") var errno i32;
extern(abi="c", name="errno") var last_error i32;
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

`name` sets the object symbol. The program reads `last_error`. That argument adds no instructions.

```c
type Real f32;

type Entity struct {
    id i32;
};

const n = 4;
var scale = 1.5f;
var wide = 20.0;
const bias = 0.5f;
const lifted f64 = bias;
var copied = scale;
var neg = -1.0;
var from_int f32 = n;
const from_lit f32 = 1;
var spare f32;
const mark = Real(1.0f);
var Entity::scale = 1.5f;

func main() i32 {
    scale = 9.0f;
    if (copied != 1.5f || wide + bias != 20.5 || lifted != 0.5 || neg != -1.0) {
        return 0;
    }
    if (from_int != 4.0f || from_lit != 1.0f || spare != 0.0f || mark != Real(1.0f) || Entity.scale != 1.5f) {
        return 0;
    }
    return n + 38;
}
```

```asm
main:
movss (%rip), %xmm0              # move
movss %xmm0, (%rip)              # move
movss (%rip), %xmm0              # move
movb $1, %al                     # constant
movss (%rip), %xmm1              # move
ucomiss %xmm1, %xmm0
movb %al, -1(%rsp)               # move
jne .L1                          # branch
movsd (%rip), %xmm0              # move
movss (%rip), %xmm1              # move
cvtss2sd %xmm1, %xmm1
addsd %xmm1, %xmm0
movsd (%rip), %xmm1              # move
ucomisd %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -1(%rsp)               # move
.L1:
movb -1(%rsp), %cl               # move
movb $1, %al                     # constant
testb $1, %cl                    # test
movb %al, -2(%rsp)               # move
jne .L2                          # branch
movsd (%rip), %xmm0              # move
movsd (%rip), %xmm1              # move
ucomisd %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -2(%rsp)               # move
.L2:
movb -2(%rsp), %cl               # move
movb $1, %al                     # constant
testb $1, %cl                    # test
movb %al, -3(%rsp)               # move
jne .L3                          # branch
movsd (%rip), %xmm0              # move
movsd (%rip), %xmm1              # move
ucomisd %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -3(%rsp)               # move
.L3:
movb -3(%rsp), %al               # move
testb $1, %al                    # test
jne .L4                          # branch
jmp .L5                          # jump
.L4:
xorl %eax, %eax                  # zero the result
retq                             # return
.L5:
jmp .L6                          # jump
.L6:
movss (%rip), %xmm0              # move
movb $1, %al                     # constant
movss (%rip), %xmm1              # move
ucomiss %xmm1, %xmm0
movb %al, -4(%rsp)               # move
jne .L7                          # branch
movss (%rip), %xmm0              # move
movss (%rip), %xmm1              # move
ucomiss %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -4(%rsp)               # move
.L7:
movb -4(%rsp), %cl               # move
movb $1, %al                     # constant
testb $1, %cl                    # test
movb %al, -5(%rsp)               # move
jne .L8                          # branch
movss (%rip), %xmm0              # move
xorps %xmm1, %xmm1
ucomiss %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -5(%rsp)               # move
.L8:
movb -5(%rsp), %cl               # move
movb $1, %al                     # constant
testb $1, %cl                    # test
movb %al, -6(%rsp)               # move
jne .L9                          # branch
movss (%rip), %xmm0              # move
movss (%rip), %xmm1              # move
ucomiss %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -6(%rsp)               # move
.L9:
movb -6(%rsp), %cl               # move
movb $1, %al                     # constant
testb $1, %cl                    # test
movb %al, -7(%rsp)               # move
jne .L10                         # branch
movss (%rip), %xmm0              # move
movss (%rip), %xmm1              # move
ucomiss %xmm1, %xmm0
setne %al                        # flag to byte
movb %al, -7(%rsp)               # move
.L10:
movb -7(%rsp), %al               # move
testb $1, %al                    # test
jne .L11                         # branch
jmp .L12                         # jump
.L11:
xorl %eax, %eax                  # zero the result
retq                             # return
.L12:
jmp .L13                         # jump
.L13:
movl $42, %eax                   # constant
retq                             # return
```

The program returns 42.

```c
func call_func() i32 {
    return 10;
}

func later() i32 {
    return item + 1;
}

var item = call_func();
var next = later();

func main() i32 {
    return item + next + 21;
}
```

```asm
main:
movl (%rip), %eax                # move
addl (%rip), %eax                # add
addl $21, %eax                   # add
retq                             # return

later:
movl (%rip), %eax                # move
addl $1, %eax                    # add
retq                             # return

call_func:
movl $10, %eax                   # constant
retq                             # return
```

`item` is stored before `next` is called, so `next` reads 10. The program returns 42.

### Tuples

Several names unpack one tuple. `var value1, value2 = mytuple` defines both names. `a, b := mytuple` defines both names, and a name already in the block is hidden. `value1, value2 = mytuple` assigns names that already exist. A call that returns several results is that tuple, so `var a, b = call` unpacks it the same way. The number of names matches the tuple. A different count is reported as `type mismatch`. The same bind may be the first clause of `for`.

```c
func step(n i32) (i32, i32) {
    return n + 1, n;
}

func main() i32 {
    var value = (40, 2);
    var value1, value2 = value;
    a, b := value;
    value1, value2 = (1, 2);
    var acc i32 = 0;
    for (k, n := step(2); k < 4; k = k + 1) {
        acc = acc + n;
    }
    return value1 + a + acc;
}
```

```asm
main:
# trace frame for the call, then the matching pop
movl $2, %edi                    # constant
callq step                       # call step
retq                             # return

step:
movl %edi, -4(%rsp)              # move
movl -4(%rsp), %edx              # move
movl %edx, %eax                  # move
incl %eax
retq                             # return
```

### Pointer and array order

A pointer is a leading star in every position, including a parameter, a result, a type alias, a cast, and `sizeof`: `*i32`, `*const u8`, `type Ptr *Point`, `sizeof(*i32)`, `*i32(value)`. An array is written in front of its element type in every position: `type Pair [2]i32`, `func head(a [4]i32)`. A slice uses the same place with no length: `[]i32` and `func head(a []i32) []i32`. `i32[4]` and `i32[]` are reported as `type mismatch`.

```c
type Pair [2]i32;

func head(a [4]i32) i32 {
    return a[0];
}

func main() i32 {
    var data [4]i32;
    data[0] = 22;
    data[1] = 20;
    var arr *[4]i32 = &data;
    var pair Pair;
    pair[0] = arr[0];
    return head(data) + (*arr)[1] + pair[0];
}
```

```asm
head:
movq %rdi, -8(%rsp)              # move
movq -8(%rsp), %rax              # move
movl (%rax), %eax                # move
retq                             # return
```

[Index](../README.md) · [Previous: defer](defer.md) · [Next: new and delete](new.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
