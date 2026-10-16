# Bindings

[Index](../README.md) · [Previous: defer](defer.md) · [Next: new and delete](new.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

A variable is `var name type`, `var name = expr`, or `name := expr`. `i32 value = 1` is reported as `expected var`. When `var` has an initializer, the type may be omitted and is taken from that value: `var value = 1`, `var p = &value`, `var p = Point{}`. The type can still be written after the name, with a pointer or an array in front of its element type: `var value i32 = 1`, `var p *i32`, `var buf [32]u8`, `var ps [2]*i32`. One `var` names one variable. A variable with no initializer keeps the type: `var value i32;`. A local with no initializer is uninitialized. It does not store 0. `p := Point{}` is the same inferred struct value.

A `*T`, or a struct or array that contains one, needs an initializer. `var p *i32;` is reported as `type mismatch`. `var p ?*i32;` is uninitialized. `var p ?*i32 = null` is `null`. A local `const` needs an initializer. `const n = 1` and `const n i32 = 1` are that binding. `const n;` and `const n i32;` are reported as `expected '='`. Assigning the local const is reported as `type mismatch`. `extern var p *i32;` is not checked. `name := expr` defines a new name. In a list, it also assigns a name that already exists in the block, and at least one name must be new. A `:=` whose names are all already in the block is reported as `type mismatch`. `var name := expr` is reported as `type mismatch`.

`*[4]i32` is a pointer to an array. `arr[1]` reads and writes the same element as `(*arr)[1]`. `&` of an array has that pointer type.

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

A local array takes a brace list with one value per element: `var row [3]i32 = {10, 20, 12}`. A different count is reported as `type mismatch`. `row[0]` reads an element.

`[_]i32{10, 20, 12}` is an array. `_` stands for the length, which is the number of values, so the type is `[3]i32`. `var row = [_]i32{10, 20, 12}`, `row := [_]i32{10, 20, 12}`, and a local `const row = [_]i32{10, 20, 12}` take that type. `var typed [2]i32 = [_]i32{4, 1}` matches the written length. A different length or a different element type is reported as `type mismatch`. Assigning an element of the const binding is reported as `type mismatch`.

```c
func main() i32 {
    var row = [_]i32{10, 20, 12};
    other := [_]i32{7, 8};
    var typed [2]i32 = [_]i32{4, 1};
    return row[0] + row[2] + other[0] + other[1] + typed[0] + typed[1];
}
```

The same lists are file-scope initializers when every value is a constant. The element may be an integer, `bool`, a float, a pointer, a struct, an enum, a typedef, or a function pointer. `var named [3]i32 = {10, 4, 1}`, `var row = [_]i32{10, 20, 12}`, `other := [_]i32{6, 1}`, and `const fixed = [_]i32{19, 2}` are those arrays. `var live [2]i32 = [_]i32{1, 2}` matches the written length. A different count is reported as `type mismatch`. An empty `[_]i32{}` has length 0. One extra level is a brace list, `var grid = [_][2]i32{{1, 2}, {3, 4}}`, and `grid[0][1]` reads an element. A global name copies that global's initializer, so `var n = 1` then `var row = [_]i32{n, 2}` keeps `1` in `row[0]` when `main` later assigns `n`. A value that is not a constant is reported as `global initializer is not a constant`. A const array decays to a pointer to a const element, so assigning an element is reported as `type mismatch`. The name is not an array length. `&fixed` is reported as `not an lvalue`.

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

The program returns 42. `row[0]` stays `1` after `n = 9`. `*q` reads the new value of `n`. `f` is a copy of `ops[0]`.

### `const`

`const name = expr` at file scope takes the type of the expression. `const name Type = expr` writes the type, and the expression has to be that type. A plain integer, such as `const N = 4`, is a compile-time integer. It has no address. The expression is folded the same way as `assert`, including `sizeof`, float arithmetic, and `==` or `!=` of two `typeof` values. A suffix whose value fits in `i32`, such as `1u32`, folds too. `const N = 4` can be an array length, `[N]i32`.

`f32` and `f64` can be that constant too. `const bias = 0.5f` is `f32`. `const lifted f64 = bias` converts the width. `const from_lit f32 = 1` converts a folded integer. The name is not an array length. Assigning it, or taking its address, is reported as `not an lvalue`. Copying a `var` into a const float is reported as `global initializer is not a constant`.

An integer or bool array can be that constant too: `const fixed = [_]i32{19, 2}` and `const flags = [_]bool{true, false}`. A float, a pointer, a struct, an enum, a typedef, or a function pointer can be the element when every value is a constant, as in `const origin = [_]Point{Point{.x = 1, .y = 2}}`. Copying a `var` into that array is reported as `global initializer is not a constant`. A read decays to a pointer to a const element. The name is not an array length, and it has no address. A struct, a function pointer, or a typedef can be that constant too. `const origin Point = {.x = 10, .y = 10}` and `const other = Integer(4)` are those types. `const origin = Point{}` is a `Point` whose fields are zero, and `const origin = Point{.x = 10, .y = 10}` fills those fields. `const op BinOp = BinOp(add)` and `const plain func (i32, i32) i32 = add` name a function. The initializer has to be a constant. A call is reported as `global initializer is not a constant`. Assigning the constant, or a field of a struct constant, is reported as `type mismatch`. A method whose receiver is a non-const pointer is reported as `type mismatch: expected *Point, found *const Point`. `self *const this` can be called on the constant. It has no address. An alias is still a different type, so `var raw i32 = value` is reported as `type mismatch` when `value` is `Integer`. `i32(value)` confirms it. An alias is not an array length.

A string literal is `*const u8`. `const label *const u8 = "unix"` and `const label = "hi"` keep that type. The name has no address. `const p *i32 = &n` is reported as `type mismatch`.

`export const name = expr` and `export const name Type = expr` publish the constant. Another package reads it as `pkg.name` or `import { name }`. Assigning it is rejected the same way as any other constant. A constant without `export` is reported as `name is not exported`. `const Entity::next = expr` is a constant on that type, written beside the struct. See [Methods](../methods/constant.md).

Inside a function, `const name = expr` is an immutable binding. The type is the type of the expression. `const name Type = expr` writes the type. `name = ...` is reported as `type mismatch`. `&name` has type `*const T`, so a store through that pointer is reported as `type mismatch`. The binding is not a compile-time integer, and it cannot be an array length. A local `const` with no initializer is reported as `expected '='`.

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

The program returns 42.

### File scope

At file scope, `var name = expr` and `name := expr` both infer the type from the initializer. `export` goes on either form: `export var bonus = 40` and `export extra := 2`. Another package reads and assigns `pkg.bonus` or `import { extra }`. `extern` and `extern(abi="c")` still go on a typed `var`: `extern var slot i32`, `extern(abi="c") var slot i32`. `extern` on `var name = expr` or `name := expr` is reported as `not supported yet`. `export` also goes on a file-scope `const` and on `var Type::name`. A global with no initializer is zero. A float literal is a constant initializer. `var scale = 1.5f` is `f32` and `var wide = 20.0` is `f64`. `var copied = scale` keeps that initializer after `scale` is assigned. `var neg = -1.0` is the negated literal. `var added = 1.5f + 0.5f` folds that arithmetic. `var from_int f32 = n` converts a folded integer, and so does `var from_u f32 = 1u32`. `var spare f32` is zero. `var Entity::scale = 1.5f` is that variable on a type. A call is reported as `global initializer is not a constant`.

```c
var count = 1;
n := 2;
export var shown i32 = 4;
var hidden i32 = 3;

func main() i32 {
    return count + n + shown + hidden;
}
```

```c
extern var slot i32;
extern(abi="c") var errno i32;
```

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

The program returns 42.

### Tuples

Several names unpack one tuple. `var value1, value2 = mytuple` defines both names. `a, b := mytuple` does the same when at least one name is new. `value1, value2 = mytuple` assigns names that already exist. A call that returns several results is that tuple, so `var a, b = call` unpacks it the same way. The number of names matches the tuple. A different count is reported as `type mismatch`. The same bind may be the first clause of `for`.

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

## Cost

`var value = 1` is 1 store of 1. `var n i32` with no initializer adds no instruction. `extra := 2` is 1 store of 2. `var p *i32 = &n` is 1 `leaq` and 1 store of that address.

A local `const value = 40 + 2` is folded and stored like `var value = 42`: 1 store of 42. It is not a compile-time integer. A file-scope `const N = 4` used as `return N` is the same 2 instructions as `return 4`. No slot is read.

A file-scope `var n i32 = 7` occupies space in the binary. `return n` is 1 `movl` from that address and `retq`. `n = n + 1` in `main` is 5 instructions: load, add 1, store, load again, return.

A tuple local `(40, 2)` is 2 stores. Unpacking it into two names is 2 loads and 2 stores. A `for` whose first clause unpacks a call pays the traced call once per check of the condition.

A local array with no initializer adds no instruction. A full list stores each listed value, 1 `movl` each, and does not store a zero first. A shorter list still stores a zero for each element it does not name. A file-scope array is initialized in the binary. Reading one element of it is 1 `movl`. A compile-time length such as `[N]i32` adds no instructions beyond the element stores.

[Index](../README.md) · [Previous: defer](defer.md) · [Next: new and delete](new.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
