# The language

`tlc` compiles a C dialect. The syntax is close to C, and the grammar is in [grammar.md](grammar.md). A function is written `func name(arg type) type`. You compile one entry file, import names a directory of source files, and types can have methods and traits. The dialect is a subset of ISO C with these additions.

Diagnostics look like `file:line:column: error: message`.

## The compiler

```text
tlc [options] <entry.tlc>
tlc --version
tlc --help
```

| Option | Meaning |
| --- | --- |
| `--check` | Parse and typecheck |
| `--emit-llvm` | Write LLVM IR |
| `--emit-obj` | Write an object file (`a.o` when `-o` is absent) |
| `--run` | Compile, link, and run |
| `-o <file>` | Output path |
| `-P <dir>` | Extra package search root, after the entry file's directory |
| `-l <lib>` | Link `lib<lib>` when producing an executable |
| `-L <dir>` | Directory searched for libraries |
| `-Bstatic` | Link later `-l` libraries from static archives (`.a`) |
| `-Bdynamic` | Link later `-l` libraries from shared libraries (`.so`) |
| `-static` | Link all libraries statically |
| `-Wl,<opt>` | Pass `<opt>` through to the linker |

`-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, and `-Wl` are passed to `cc` after the object file, in the order written. A path that ends in `.a`, `.so`, or `.dylib`, and a versioned shared library such as `libfoo.so.1`, is a library file and is passed through the same way. `-Bstatic` stays in effect until `-Bdynamic`, so put `-Bdynamic` after the static libraries when the C library should stay shared. `--check`, `--emit-llvm`, and `--emit-obj` accept the flags and do not link.

Vendor packages are imported as `vendor/<name>`. Set `TLC_LIB` to choose their directory. When that variable is unset, the directory is `lib/tlc/vendor` next to the compiler prefix: `bin/tlc` and `build/tlc` both use `<prefix>/lib/tlc/vendor`. The entry directory and each `-P` root are searched first, using the full path `vendor/<name>`.

The file on the command line is the entry. Its `main` is the program's `main`. A symbol is named `qualifier.name`. In a package with more than one file, a `static` symbol is named `qualifier.filename.name`, where `filename` is the source file without `.tlc`. `extern` keeps the C name.

## A program

```c
func main() int {
    return 40 + 2;
}
```

```sh
tlc --run main.tlc
```

The exit status is 42.

Comments are `//` to the end of the line and `/* ... */`. The language has no preprocessor and no `#include`.

## Packages

The file on the command line is its own package. Other `.tlc` files next to it are not compiled with it. An import names a directory. Every `.tlc` file directly in that directory is one package, and a subdirectory is a different package.

The qualifier is the `package` name when every file in the directory agrees. When no file has a `package` clause, the qualifier is the directory's name. Clauses that disagree produce `package mismatch`. The entry file's qualifier is its `package` name, or the file name without `.tlc`.

```c
package math;

export func add(a int, b int) int {
    return a + b;
}
```

`package` comes first, before imports and declarations. The name is an identifier.

Imports come after `package` and before every declaration. There are three forms:

```c
import "math";
import "math" as m;
import { add, Point } from "math";
```

`as` and `from` are ordinary words in those positions. They are identifiers everywhere else.

The first form binds the package qualifier as a name, so the call is `math.add(40, 2)`. The alias makes it `m.add(40, 2)`. The selective form brings the named exports into the file, so the call is `add(40, 2)`. An import is private to the file that wrote it.

The path is a string and names a directory. An empty path and an absolute path are rejected. A path that is not a directory, including one that ends in `.tlc`, produces `unknown package`.

- `./` and `../` are resolved relative to the importing file and must name a directory.
- Every other path is searched in the entry file's directory, then in each `-P` root. A path `vendor/<name>` that is not found there is then loaded from the vendor directory. The first directory that contains a `.tlc` file wins.
- A directory with no `.tlc` file produces `unknown package`.

The vendor directory holds packages that ship with the compiler, such as a binding to a C library. `import "vendor/sdl3"` loads `<vendor>/sdl3`.

```text
lib/tlc/vendor/sdl3/
    video.tlc
    libSDL3.a
    libextra.so
    link
```

`examples/triangle` is a program that imports `vendor/sdl3` and `vendor/gl`. Each of those packages names a system library in its `link` file.

Every `.tlc` file in that package is compiled with the program. A static archive (`.a`) and a shared library (`.so`, `.dylib`, or a versioned name such as `libfoo.so.1`) in the same directory is linked into the executable. When one library name has both an archive and a shared library, the archive is linked. Shared libraries are found through an rpath that points at the package directory. The same package imported from several files is linked once.

A file named `link` in the package adds more linker arguments after those libraries. Each line holds the same flags as the command line: `-l`, `-L`, `-Bstatic`, `-Bdynamic`, `-static`, `-Wl`, and library paths. `#` comments out the rest of the line. A relative path is resolved from the package directory. `$ORIGIN` in an rpath is left as written. `--check`, `--emit-llvm`, and `--emit-obj` load the package and do not link.

An import cycle produces `circular import` followed by the qualifiers, for example `a -> b -> a`. The same package may be imported from several files and is loaded once.

Inside a package, a name that is not `static` is visible from every file in the directory. The same non-static name in two files is `redefinition`. `static` stays private to its file, so two files may each declare `static` with the same name.

## Visibility

Only a file-scope `export` is published to importers. A name that was not exported produces `name is not exported`.

```c
package math;

func secret() int {
    return 1;
}

export func add(a int, b int) int {
    return a + b + secret();
}
```

`secret` can be called from any file in the package. An importer sees `add`.

`export` applies to functions, variables, structs, enums, and typedefs. `export static` is an error. `export extern` is allowed on a function, and the symbol keeps its C name. `export extern` on a variable is an error.

File-scope `static` keeps the symbol private to that file. A `static` local is rejected.

`extern` keeps the C name and the C calling convention. It returns one result, the same as a C function. Its parameters and its result cannot include a function pointer that returns several results. A struct used by an `extern` function, by value or through a pointer, must itself be `extern`. That includes a struct nested inside it. An `extern struct` uses the platform C memory layout, so its fields line up with a static or shared library. `bool` in that struct is one byte. On this host a struct of at most 16 bytes is passed in registers, and a larger struct is passed as a pointer. A function that is not `extern` keeps the compiler's own convention for struct values. That is how a program calls a library:

```c
extern func printf(fmt const char *, ...) int;

func main() int {
    printf("hi\n");
    return 0;
}
```

`const` is accepted on parameters of an `extern` declaration. A variadic function may be declared with `extern` and without a body. A definition that uses `...` is rejected.

## Types

Integers on `x86_64-pc-linux-gnu` follow LP64. `long` and `i64` are both 64 bits. They are different types.

| Type | Bits |
| --- | --- |
| `char` | 8 |
| `i8`, `u8` | 8 |
| `short`, `i16`, `u16` | 16 |
| `int`, `uint`, `i32`, `u32` | 32 |
| `long`, `ulong` | 64 |
| `i64`, `u64` | 64 |
| pointer | 64 |

`signed` and `unsigned` are not keywords. `long long` is `type mismatch`. Write `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, and `u64`. `short int` is `short`. `long int` is `long`. Contradictory specifiers are `type mismatch`.

`char`, `i8`, and `u8` are three types. They are assignment-compatible. `int *` and `uint *` are different types.

`i8` and `u8` are the 8-bit integers. `i16` is `short` and `u16` is its unsigned counterpart. `i32` is `int` and `u32` is `uint`. `i64` and `u64` are the other 64-bit integers. `ulong` is 64 bits and a different type from `u64`. `long` stays a different 64-bit type from `i64`. `char` stays different from `i8`.

The rank order is `bool`, then `char`, `i8`, and `u8`, then `short` and `u16`, then `int`, `long`, and `i64`. An unsigned type has the same rank as its signed counterpart.

A type whose rank is below `int` widens to `int` when it is used as a value. `i8` is sign-extended. `char` and `u8` are zero-extended. The usual arithmetic conversions apply after that. `+`, `-`, `*`, `/`, `%`, and the bitwise operators produce the common type. Comparisons and `!` produce `int`, either 0 or 1.

Unsigned arithmetic wraps. A `uint` that is 0 and then decreased by 1 becomes `4294967295`, so `n < 1` is false.

A mixed comparison converts the signed operand. `1u32 < -1` is true, because `-1` becomes the largest unsigned value.

Assignment is allowed among the integer ranks, `char`, and `bool`. A store into `bool` becomes 0 or 1. A store into a narrower integer keeps the low bits.

`void` is a return type and a pointee. It is neither a value nor a variable type.

`float` and `f32` are the same 32-bit type. `double` and `f64` are the same 64-bit type. A literal without a suffix has type `double`. A suffix `f` or `F` makes it `float`. `1.5 + 1` is `double`. Comparisons of floating values yield `int`. `%` and bitwise operators are `type mismatch` on a floating value.

A `bool` value is 0 or 1.

## Literals

| Literal | Type |
| --- | --- |
| `42` | `int` |
| `42i8`, `42u8` | `i8` and `u8`, when the value fits |
| `42i16`, `42u16` | `i16` and `u16`, when the value fits |
| `42i32`, `42u32` | `int` and `uint`, when the value fits |
| `42i64`, `42u64` | `i64` and `u64`, when the value fits |
| `'A'` | `char` |
| `"hi"` | array of `char`, including the terminating null |
| `` `hi` `` | the same array type; the text may span lines |
| `true`, `false` | `bool` |
| `nullptr` | null pointer |

An unsuffixed number greater than `2147483647` is `type mismatch`. It does not become `i64`. A suffix is one of `i8`, `u8`, `i16`, `u16`, `i32`, `u32`, `i64`, and `u64`. `1u` and `1l` are `invalid integer suffix`. A value that does not fit in the suffix is `type mismatch`.

A character literal has type `char`. `'A'` is 65 and `'\n'` is 10. A value above 255 is `type mismatch`. The escapes are `\n`, `\t`, `\r`, `\\`, `\'`, `\"`, `\a`, `\b`, `\f`, `\v`, `\?`, octal digits, and `\x` followed by hex digits.

A string between backticks may span lines. It is an array of `char`, like a quoted string. The characters are copied as written: a backslash is not an escape, and a newline in the source is a newline in the string. The next backtick ends the string.

```c
var text *char = `hello
world`;
```

`sizeof` produces `int`. `sizeof(char)` is 1, `sizeof(int)` is 4, `sizeof(i64)` is 8, and a pointer is 8. Both `sizeof(int)` and `sizeof n` are accepted.

## Expressions

The operators are `+ - * / %`, `& | ^ ~ << >>`, `&& || !`, the comparisons, unary `+` and `-`, assignment `=`, calls, indexing, `.`, address-of `&`, and dereference `*`.

`&&` and `||` short-circuit. In the program below, `bump` is called twice and `main` returns 2.

```c
var calls int = 0;

func bump() int {
    calls = calls + 1;
    return 1;
}

func main() int {
    if (0 && bump()) {}
    if (1 || bump()) {}
    if (1 && bump()) {}
    if (0 || bump()) {}
    return calls;
}
```

A shift is evaluated in the common type.

Pointers of the same type can be compared with `==` and `!=`. A pointer can be compared with `nullptr`, and `nullptr == nullptr` is allowed. The result is `int`. Pointers of different types cannot be compared. Relational operators on pointers are rejected. `p == 0` is `type mismatch`.

An integer literal `0` may be assigned to a pointer, and the pointer becomes null. `nullptr` may be assigned to any pointer. Assigning `nullptr` to `int` or `bool` is `type mismatch`. `true` and `false` do not convert to pointers.

A condition in `if`, `while`, `do`, and `for` is an integer after promotion. A pointer used as a condition is `type mismatch`. `switch` requires exactly `int`.

A cast is the type name followed by the value in parentheses: `i32(n)`, `int(n)`, `char*(bits)`, `void*(p)`. A cast between integer types is allowed. Signed `int` can be cast to and from a pointer. `nullptr` can be cast to a pointer. `long(p)`, `uint(p)`, and `int(nullptr)` are `type mismatch`. A cast to `void` is rejected.

Pointer arithmetic is `type mismatch`. A subscript index has type `int`.

`++`, `--`, compound assignment, `?:`, and `->` are rejected.

## Statements

Blocks, `if`/`else`, `while`, `do`/`while`, `for`, `switch`, `break`, `continue`, `return`, and empty statements are accepted. `goto` is rejected.

`for` has C's three clauses. A declaration in the first clause is visible only inside the loop. That clause may also bind several results with `var`, `:=`, or `=`. A comma in the name list must be followed by another name.

```c
func main() int {
    var s int = 0;
    for (var i int = 1; i <= 4; i = i + 1) {
        s = s + i;
    }
    return s;
}
```

`switch` falls through until `break`. A `case` label is a constant `int`. `case 1u32` is `type mismatch`. Each value appears once, and `default` appears once.

```c
func main() int {
    var n int = 1;
    var x int = 0;
    switch (n) {
    case 1:
        x = 2;
    case 2:
        x = x + 40;
        break;
    default:
        x = 7;
        break;
    }
    return x;
}
```

This returns 42. `break` leaves the innermost loop or `switch`. `continue` is valid inside loops.

`defer` runs one call when the enclosing block is left. The call can be a function, a method, an inner function, or an anonymous function. Calls in the same block run in reverse order. A `defer` whose statement did not run does not run. `return`, `break`, `continue`, and reaching the end of the block all run the calls of the blocks they leave. A `return` expression is evaluated before those calls. Arguments, and a method receiver, are evaluated at the `defer`.

```c
func main() int {
    defer func(value int) {
    }(10);
    return 0;
}
```

An anonymous function is an expression, `func (parameters) { body }`. The return type may be omitted, and then it is `void`. A return type is written after the parameters when it is present: `func () int { return 1; }`. Several results use the same parenthesized list as a named function: `func () (int, int) { return 1, 2; }`. The call is the argument list after the body. It does not capture a local or a parameter from an enclosing function.

A variable is `var name type`, `var name = expr`, or `name := expr`. `int value = 1` is `expected var`. When `var` has an initializer, the type may be omitted and is taken from that value: `var value = 1`, `var p = &value`. The type can still be written after the name, with a pointer or an array in front of its element type: `var value i32 = 1`, `var p *int`, `var buf [32]u8`, `var ps [2]*int`. One `var` names one variable. A variable with no initializer keeps the type: `var value i32;`. `name := expr` defines a new name and can also assign a name that already exists. `var name := expr` is `type mismatch`. `*[4]i32` is a pointer to an array. `arr[1]` reads and writes the same element as `(*arr)[1]`. `&` of an array has that pointer type. Two stars outside the array, as in `**[4]i32`, are `not supported yet`.

At file scope, `var name = expr` and `name := expr` both infer the type from the initializer. `var a, b = expr` at file scope is `not supported yet`. `export`, `static`, and `extern` go on a typed `var`: `export var n int = 1`, `static var n = 1`, `extern var slot int`. `export` or `extern` on an inferred `var name = expr` or `name := expr` is `not supported yet`.

Several results are still bound together: `var a, b = call`. A local struct, enum, or typedef is rejected. Fields, parameters, typedefs, and return types keep their own syntax.

A global with no initializer is zero. An array cannot be initialized with `=`.

## Functions

A function starts with `func`, then the name, then the parameters, then the return type. A parameter is a name followed by its type. An empty parameter list means there are no parameters.

```c
func add(a int, b int) int {
    return a + b;
}

func main() int {
    return add(20, 22);
}
```

A function can return several results. The result list is parenthesized, and each result is a type. One type in parentheses is the same as writing that type alone. `return` then lists one expression per result.

```c
func add(x int, y int) (int, int) {
    return x + y, x;
}
```

The call is not itself one value. Bind the results with `var`, with `:=`, or with `=` when every name already exists:

```c
func main() int {
    sum, orig := add(20, 22);
    var extra, base = add(1, 1);
    extra, base = add(0, 0);
    return sum;
}
```

`var` defines every name in the current block. `:=` defines a name that is new in the current block and assigns a name that already exists there. At least one name must be new. A name from an outer block is shadowed. The same bind may be the first clause of `for`. A call used as a statement, or as the call in `defer`, discards every result. The call may be a function, a method on a field, a subscript, a dereference, or a temporary value. An `extern` function cannot return several results. A trait method cannot either. A type error names the expected type and the found type.

A function may be declared first and defined later, with the same type. Two bodies for the same function are `redefinition`.

A function may also be written inside a block. Its name is visible from that declaration through the end of the block, including blocks nested inside it. A prototype in that block must be followed by a body in the same block. The function can call functions already in scope, and it can use globals. A use of a local or a parameter from an enclosing function is rejected. `export`, `static`, `extern`, and a method are rejected on a function in a block. An anonymous function, used from `defer` or called directly, follows the same rule.

```c
func main() int {
    func add(a int, b int) int {
        return a + b;
    }
    return add(20, 22);
}
```

Structs are passed and returned by value. An array parameter becomes a pointer, both as `a int[]` and as `a int[4]`.

A function cannot return an array. There is one `main`.

A function pointer type is `func (argument types) result`. The arguments are types, with no names. The result is written the same way as a function result: one type, or a parenthesized list of several types. A function name used as a value is a pointer to that function.

```c
func add(a i32, b i32) i32 {
    return a + b;
}

func myfunc(ptr func (i32, i32) i32) i32 {
    return ptr(20, 22);
}
```

A variable names the pointer first: `var fp func (i32, i32) i32;` or `var fp func (i32) (i32, i32);`. A field or a typedef still writes the type, then the name: `func (i32, i32) i32 op;` inside a struct, and `typedef func (i32, i32) i32 BinOp;`. A function pointer whose result is itself a function pointer is not supported yet. An `extern` function's parameters and result cannot include a function pointer that returns several results.

A function or a method that takes a function pointer accepts an anonymous function as that argument. The full form writes the parameter types and the result:

```c
func dosomething(op func (int, int) int) int {
    return op(40, 2);
}

func main() int {
    return dosomething(func (arg1 int, arg2 int) int { return arg1 + arg2; });
}
```

The short form writes only the parameter names. Their types, and the result type, come from the function pointer:

```c
return dosomething((arg1, arg2) { return arg1 + arg2; });
```

`() { return 1; }` is the short form with no parameters. The short form is also accepted where a value is checked against a known function pointer, including an assignment and the initializer of a typed variable. Used where no function pointer is expected, it is `type mismatch`. A short form whose parameter count does not match that function pointer is `type mismatch`. It follows the same capture rule as any other anonymous function.

## Structs, pointers, and arrays

A struct definition uses the `struct` keyword. Variables, parameters, return types, and fields then use the bare tag. The semicolon after `}` may be omitted.

```c
struct Point {
    int x;
    int y;
};

func make(x int, y int) Point {
    var p Point;
    p.x = x;
    p.y = y;
    return p;
}

func main() int {
    var p Point;
    var q *Point;
    p = make(40, 2);
    q = &p;
    return (*q).x + q.y;
}
```

This program returns 42.

A struct may contain a pointer to itself. A by-value field of the struct that is being defined is rejected.

```c
struct Node {
    int n;
    Node *next;
};
```

A struct or an enum is defined by its body. `struct Node;`, `typedef struct Node;`, `extern struct Node;`, `enum Color;`, and `extern enum Color;` are `expected '{'`. A struct defined in another file of the same package is already visible, including through a pointer, so a separate declaration is not required.

An `extern struct` is marked so it can be used with an `extern` function. An empty body is an opaque handle: the type has no fields and occupies one byte, and a library uses it through a pointer. An empty struct that is not `extern` is `not supported yet`. An empty enum is `not supported yet`.

```c
extern struct Pair {
    int a;
    int b;
};

extern struct SDL_Window {}

extern func make_pair(a int, b int) Pair;
extern func SDL_DestroyWindow(window SDL_Window *) void;
```

A plain struct in an `extern` function is `struct must be extern`.

Values and pointers both use `.`. A pointer whose pointee is a struct is dereferenced once. `Point **` and `int *` are left as they are, and the access is `type mismatch`. A field reached through a pointer is an lvalue. Assigning a field of a struct value requires the struct to be an lvalue.

An array length is a positive decimal `int` literal.

```c
func add4(p int *) int {
    return p[0] + p[1] + p[2] + p[3];
}

func main() int {
    var a [4]int;
    a[0] = 10;
    a[1] = 20;
    a[2] = 7;
    a[3] = 5;
    return add4(a);
}
```

The array becomes a pointer when it is passed. A string is an array of `char` and can be passed to `char *`. The index has type `int`. Multidimensional arrays are rejected.

An exported struct is named with the package and without the `struct` keyword:

```c
import "geom";

func main() int {
    var p geom.Point;
    p.x = 40;
    p.y = 2;
    return geom.sum(p);
}
```

```c
package geom;

export struct Point {
    int x;
    int y;
};

export func sum(p Point) int {
    return p.x + p.y;
}
```

Inside the package the type is written `Point`. `import { Point } from "geom"` makes `Point` a type name in the importing file. A struct tag is visible to every file in the package, so one file can declare `struct A { B b; }` when `struct B` is in another file of that package.

## Methods

A method belongs to a struct or an enum in the same package. The first parameter is the receiver. It is written with `this`, then the name, then the type. `this` is an ordinary identifier in every other position. On an enum, the receiver is that enum's value.

The call omits the receiver. This program returns 42.

```c
struct Point {
    int x;
    int y;
};

func Point::sum(this self Point, extra int) int {
    return self.x + self.y + extra;
}

func Point::add(this self Point *, dx int, dy int) void {
    self.x = self.x + dx;
    self.y = self.y + dy;
}

func main() int {
    var p Point;
    p.x = 10;
    p.y = 10;
    p.add(1, 1);
    return p.sum(20);
}
```

`add` takes a pointer, so `p` must be an lvalue. The compiler passes `&p`. `sum` takes a value. A call on a `Point *` loads the struct. A pointer method on a temporary struct value is `not an lvalue`.

The receiver may have one star. `this self Point **` is rejected.

The same method name may exist on different structs and as a free function. The call `p.sum(...)` selects the struct's method. A free call `sum(...)` selects the function.

A method is visible from every file in its package. It is not exported, so another package cannot call it. `export` on a method is `not supported yet`. `Point::sum(p, 1)` is not a call.

## Traits

A trait is a list of methods. The receiver is implicit and is omitted from the trait type.

```c
trait Shape {
    func area() int;
    func grow(n int) void;
};

struct Rect {
    int w;
    int h;
};

func Rect::area(this self Rect) int {
    return self.w * self.h;
}

func Rect::grow(this self Rect *, n int) void {
    self.w = self.w + n;
    self.h = self.h + n;
}

func measure(s Shape *) int {
    return s.area();
}

func apply(s Shape *, n int) int {
    s.grow(n);
    return measure(s);
}

func main() int {
    var r Rect;
    r.w = 5;
    r.h = 6;
    return apply(&r, 1);
}
```

`apply(&r, 1)` changes the width and height to 6 and 7. `area` returns 42.

There is no `impl` keyword. A struct satisfies a trait by defining the methods, with the same return type and the same parameters after the receiver. The check happens where a concrete struct value or a concrete pointer is passed to a trait parameter.

- `s Shape` requires every method to have a value receiver.
- `s Shape *` accepts both value receivers and pointer receivers.
- A pointer method passed as a value is `type mismatch`.
- A missing method is `undeclared identifier`.
- A wrong return type or wrong extra parameters are `type mismatch`.
- The address is not taken automatically. `apply(r)` is `type mismatch` when the parameter is `Shape *`. The call is `apply(&r)`.

A trait type is allowed only as a function parameter, either `Shape` or `Shape *`. A local, a global, a return type, an array, a typedef, and `sizeof` of a trait are rejected. `Shape **` is rejected.

The function is monomorphized. Each struct that is passed in gets its own copy, and the call `s.area()` becomes a direct call of that struct's method. Passing the same trait onward, as `measure(s)` inside `apply`, is not checked again.

`this` inside a trait method is `type mismatch`. A body in the trait is rejected. A function with a trait parameter cannot be `export`, `extern`, or variadic, and a method cannot take a trait parameter.

`s.area` without a call is `undeclared identifier`.

## Enums and typedefs

An enum is its own type. A value is named with the type, a dot, and the enumerator. It is not an `int`, and it does not convert to its representation. The semicolon after `}` may be omitted.

```c
enum Color {
    Red,
    Green = 40,
    Blue,
};

func main() int {
    value := Color.Blue;
    if (value == Color.Blue) {
        return 1;
    }
    return 0;
}
```

`Red` is 0, `Green` is 40, and `Blue` is 41. Without an explicit value, the next constant is one higher than the previous one. The representation is `int` when the definition does not name a primitive. A primitive is written between the name and the body:

```c
enum Tiny i8 {
    A,
    B,
};
```

`sizeof(Tiny)` is 1. The primitive may be `bool` or an integer type, including `i8`, `u32`, and `char`. A value that does not fit is `type mismatch`. An empty enum is rejected. An enumerator is not an lvalue. `==` and `!=` compare two values of the same enum and yield `int`. Other operators are `type mismatch`.

`extern enum` is the C form. It has no underlying type of its own; the representation is `int`, and a value may be passed or compared where an integer is expected. `extern enum Name i8` is `extern enum`. Methods are still allowed:

```c
extern enum Flag {
    Off,
    On = 1,
}

func Flag::enabled(this self Flag) int {
    if (self == Flag.On) {
        return 1;
    }
    return 0;
}
```

An exported enum exports the type. The enumerators come with it:

```c
import { Color } from "geom";

func main() int {
    value := Color.Blue;
    return 0;
}
```

A method uses the same form as a struct method. `this` is the enum value.

```c
func Color::code(this self Color) int {
    if (self == Color.Blue) {
        return 41;
    }
    return 0;
}
```

`value.code()` passes `value`. The method is visible in the package and is not exported.

```c
typedef Point *Ptr;
typedef int Integer;
typedef int Pair[2];
```

A typedef gives another name to a type. `typedef Point Point` is rejected, because the alias repeats the struct tag. When a typedef and a struct tag share a name, as in `typedef int Point` beside `struct Point`, the bare name uses the typedef, and `struct Point` still names the tag. That is the only use of `struct` outside a definition. An exported typedef is imported as a type name.

## Reserved words

Beyond C, `import`, `export`, `package`, `trait`, `func`, `defer`, and `var` are keywords. `impl`, `from`, `as`, and `this` are identifiers.
