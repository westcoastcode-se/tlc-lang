# Grammar

[Index](README.md)

This page is the syntax of tlc. Meaning, types, and diagnostics are in the [language reference](README.md). A form that this grammar does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

## Notation

A grammar rule adds no instructions. [Cost](cost.md) is how the counts on the feature pages were measured.

The grammar is EBNF. A name is a rule. `"text"` is a terminal. `[ x ]` is optional, `{ x }` is zero or more, and `( x )` groups. `|` separates alternatives. When two alternatives could match, the parser tries the earlier one.

Spaces, tabs, newlines, `//` comments, and `/* */` comments are ignored between tokens.

## Source file

A source file, a `package` clause, and an import add no instructions. They are resolved while compiling.

```ebnf
file        = [ package ] { import } { declaration } ;
package     = "package" identifier ";" ;
import      = "import" string [ "as" identifier ] ";"
            | "import" "{" [ name-list ] "}" "from" string ";" ;
name-list   = identifier { "," identifier } ;
```

`as` and `from` are ordinary identifiers in every other position.

A declaration is one of the following.

```ebnf
declaration = function
            | method
            | var-decl
            | const-decl
            | type-decl
            | assert-stmt
            | test-decl ;
```

`test` is a file-scope declaration. A `test` inside a function is `test is only valid at file scope`.

`unsafe` may stand with `export` and `extern`, in any order, in front of `func`. `unsafe func` makes the body an unsafe context. A call is `unsafe name(...)`. `export` and `extern` may stand, in any order, in front of a function, a method, a variable, or a `type`. `export` may also stand in front of a file-scope `const`, in front of `const Type::name`, in front of `var Type::name`, in front of `var name = expr`, and in front of `name := expr`. `export const Type::name` publishes that constant, including on a generic struct. Another package reads `pkg.Type.name` or `pkg.Box[i32].name`. `export var Type::name` publishes that variable, including `export var Box[T]::extra`. Another package reads and assigns `pkg.Entity.next` or `pkg.Box[i32].extra`. A `const` inside the braces is not a member and is reported as `not supported yet`. `extern` and `extern(abi="tlc")` on a file-scope `const` or `const Type::name` name `qualifier.name` or `qualifier.Type.name`. `extern(abi="c")` names the C symbol. A use still folds. `extern` on `var name = expr` or `name := expr` is reported as `not supported yet`. `extern` may be followed by `(abi="c")` or `(abi="tlc")`. A bare `extern` means `abi="tlc"`. `extern(abi="c")` names a C library symbol and is linked only together with a C library (`-l` or a library file). `extern(abi="tlc")` names a tlc library symbol. `extern(abi="c", name="symbol")` sets the C object symbol and leaves the identifier in the program unchanged. The arguments may appear in either order. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are reported as `name requires abi="c"`. `extern(abi="c")` on a trait or a generic is reported as `extern(abi="c") on a trait or a generic`. `extern(abi="c") type Name union(Enum)`, `name union(Enum)`, and a field whose type is a tagged union, inside an `extern(abi="c")` struct or union, are reported as `extern(abi="c") on a tagged union`. `extern(abi="c", name="symbol")` on a `type` is reported as `not supported yet`. An empty name is `type mismatch`.

## Functions and methods

A function definition adds the instructions of its body. A call pushes a trace frame: 36 instructions in the caller when fewer than 32 frames are stored, plus the callee. The sequences are on [Functions](functions/function.md).

```ebnf
function    = [ "unsafe" ] "func" identifier [ type-params ] "(" [ parameters ] ")" ( result ( block | ";" ) | block ) ;
comptime-func = "comptime" "func" identifier [ type-params ] "(" [ parameters ] ")" ( result ( block | ";" ) | block ) ;
macro-func  = "macro" "func" identifier [ type-params ] "(" [ parameters ] ")" ( result ( block | ";" ) | block ) ;
type-params = "[" type-param { "," type-param } "]" ;
type-param  = identifier "type"
            | identifier integer-name
            | identifier identifier ;
method      = [ "unsafe" ] "func" identifier [ method-params ] "::" identifier [ type-params ] "(" [ parameters ] ")" ( result ( block | ";" ) | block ) ;
method-params = "[" identifier { "," identifier } "]" ;
receiver    = identifier [ "*" ] "this" ;

parameters  = parameter { "," parameter } [ "," "..." ] ;
parameter   = identifier [ "comptime" ] placed-type ;

result      = placed-type
            | "(" placed-type { "," placed-type } ")" ;
```

A `comptime func` calculates while compiling. Every argument is a comptime value, whether or not the parameter is marked `comptime`. A comptime value is a folded integer or a constant `[]const u8`: a literal, a constant range, or a same-file file-scope `const`. A `var` and a local `const` are not. `return` leaves the function. A call used as a value folds when the body is one `return` of a constant `i32`. That call has no runtime symbol and pushes no trace frame. `return add(41)` when `add` is `comptime func add(n i32) i32 { return n + 1; }` is `movl $42, %eax` and `retq`. `emit` is not valid in a `comptime func`. A `var` passed to a `comptime func`, a type parameter, a method, `extern`, `export`, and a call that does not fold are `not supported yet`.

A `macro func` generates statements. A statement call is replaced by the statements written with `emit`. A parameter without `comptime` is replaced by the argument expression, so `hello(name)` emits `printf("Hello %s\n", name)`. `name comptime []const u8` and `n comptime i32` are values known while compiling, and `emit` receives that value. `const label = "Per"` then `hello(label)` emits `printf("Hello %s\n", "Per")`. `show(40 + 2)` emits the integer `42`. A `var` passed to a `comptime` parameter is `not supported yet`. `emit while` and `emit do` paste that loop. A plain `while` or `do` in a `macro` is `not supported yet`. In a `macro`, `for` and `if` run while compiling. `for (var i = low; i < high; i = i + step)` unrolls when `low`, `high`, and `step` fold to integers. `if (condition)` keeps one arm when the condition folds, including `typeof(values[i]) == typeof(i32)`. The other arm is discarded. A condition or a bound that does not fold is `not supported yet`. `for (var item : items)` in a `macro` unrolls when the length is known. A slice `var` and `ptr_of` are `not supported yet`. Outside a `macro`, `for` and `if` run as usual. `comptime for` and `comptime if` are `not supported yet`. A value call of a `macro` is `not supported yet`. A method, `extern`, and `export` are `not supported yet` on a `macro`. A type parameter is accepted, so `macro func show[T type](values comptime T)` can read that tuple.

In a `macro`, `for (var i = low; i < high; i = i + step)` unrolls when `low`, `high`, and `step` fold to integers, `step` is positive, and the trip count is at most 32. The index in the body is that integer. `emit for`, `emit if`, `emit while`, and `emit do` paste that statement into the caller, and the body is ordinary code. Those forms outside a `macro` are `not supported yet`. A plain `while` or `do` in a `macro` is `not supported yet`.

An ordinary function may mark a parameter `comptime`. That argument has to be a comptime value, or the call is `type mismatch`. When every parameter is `comptime` and the body is one `return` of a constant `i32`, the call folds to that integer. `return add(1)` when `add` is `func add(n comptime i32) i32 { return n; }` is `movl $1, %eax` and `retq`. A mix of `comptime` and runtime parameters keeps the call and passes the comptime argument as that constant. `comptime { ... }` inside an ordinary function is `not supported yet`.

A generic function may declare the same name again with `name comptime Type` on a parameter. That overload is chosen when every `comptime` argument is a comptime value, and it is preferred over the overload without `comptime`. A `var` does not choose it. When only the `comptime` overload matches the types and an argument is not a comptime value, the call is `type mismatch`. `builtin.memeql[u8]` and `builtin.memcmp[u8]` fold two constant `[]const u8` values through the `comptime` slice overload. The functions are ordinary tlc in `lib/tlc/builtin`. `==`, `!=`, and `+` on `[]const u8` are `type mismatch: expected an integer, found []const u8`.

A function with no body is a prototype and ends with `;`. One name may have several parameter lists. A call uses the types of the arguments. The receiver is not one of those parameters. A body replaces that semicolon. `...` is allowed on an `extern(abi="c")` declaration and on a function with a body. Inside that body, `builtin.va()` reads the extra arguments. One parenthesized result is the same type as the bare result. `()` as a result list is an error. A result may be a function pointer: `func pick() func (i32, i32) i32`. `pick()` is that pointer, and `pick()(1, 2)` calls it. A method uses the same result. A body may omit that result. The type is taken from the `return` expressions, and every `return` has to agree. An omitted result may be a function pointer. `func pick() { return add; }`, when `add` is `func (i32, i32) i32`, is that pointer. A method omits it the same way. That pointer may take `[]const u8`. `func pick() { return len_text; }`, when `len_text` is `func ([]const u8) i32`, is that pointer. `func pick() func ([]const u8) i32` writes the same type. One expression uses that expression's type. Several expressions in one `return` are several results when every `return` agrees. A prototype still writes the result: `func add(a i32, b i32);` is `expected a type`. `typeof` is not a result. `func id(x i32) typeof(x)` is `expected a type` on `typeof`, and `(typeof(x))` is the same error. A body with no `return` is `void`, and so is a body that only writes `return;`. The first `return` chooses the type. A later `return` of a different type is `return type: expected i32, found bool`. A `return;` beside `return 1` is `return type: expected void, found i32` when the bare `return;` is first. `func give() { return touch(); }` is `void` when `touch` is `void`, and so is `func give() void { return touch(); }`. An omitted result that is `Shape` or `*Shape` is `trait has no size`, the same as `func id(s Shape) Shape`. A `return` that calls a function whose result is not known yet does not choose the type. `func pick(flag bool) { if (flag) { return 40; } return pick(true); }` is `i32`. `func f(something bool) { if (something) { return 1; } something = true; return f(something); }` is `i32`. `func f() { return f(); }` is `infinite loop`. `func a() { return b(); }` and `func b() { return a(); }` are `infinite loop` too. That pointer's result may be a function pointer. `func outer() { return id; }`, when `id` is `func (i32) func (i32) i32`, is that pointer. `func outer() func (i32) func (i32) i32` writes the same type. An anonymous function that omits the result is `void`. The returned value may be a function name or an anonymous function, `return (a, b) { return a + b; }` or `return func (a i32, b i32) i32 { return a + b; }`. An inner function called directly may return a function pointer taken from an enclosing function.

### Methods

A receiver parameter is written `name this`, `name *this`, `name *const this`, `name ?*this`, or `name ?*const this`. `name **this` is reported as `expected 'this'`. `this` stands for the method's struct, enum, or type alias, so a pointer is `*this` and an optional pointer is `?*this`. `*const this` is a pointer to a constant value. A const variable cannot call `*this` or `?*this`. Those calls are reported as `type mismatch: expected *Point, found *const Point` and `type mismatch: expected ?*Point, found *const Point`. `this` is an ordinary identifier everywhere else. When the parameter list has no receiver, the function is static and the call is `Type.name(arguments)`. On a generic struct the call is `Vec[i32].name(arguments)` or `Buf[4, i32].name()`.

The method is written in the same file as that struct, enum, or slice alias. A method in another file is reported as `method in another file`. Every file in the package can call it. `export func Type::name` publishes a value method, and another package calls `value.name(...)`. With no receiver, `export` publishes `pkg.Type.name(...)`. On a generic struct that call is `pkg.Vec[i32].name(...)`. A method without `export` is reported as `name is not exported`. `extern func Type::name` names that method in a library. The call stays `value.name(...)`, or `Type.name(...)` when there is no receiver. `extern func Vec[T]::get` names that method. The call stays `value.get(...)`, or `Vec[i32].name(...)` when there is no receiver. Each specialization is its own symbol. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that method are reported as `extern(abi="c") on a trait or a generic`. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are reported as `name requires abi="c"`. `extern func Shape::twice` names a trait default. The call stays `s.twice()` or `r.twice()`. Each struct that gains the default has its own symbol. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are reported as `extern(abi="c") on a trait or a generic`. `extern(name="symbol")` is reported as `name requires abi="c"`. A prototype with no body is reported as `not supported yet`. `extern func Type::name[T type]` names that method template. The call stays `value.name[i32](...)`. Each specialization is its own symbol. A body defines that symbol. `extern(abi="c")` on that template is reported as `extern(abi="c") on a trait or a generic`. `extern(name="symbol")` on that template is reported as `name requires abi="c"`. A prototype with no body on that template is reported as `not supported yet`. A slice alias uses `self this` or `self *this`. `self *this` is a pointer to that slice.

An inner function uses the same `function` form inside a block, and it has a body. A type-parameter list after the method name, `func Type::name[T type](self *this) T`, is a method template when that type is not a generic struct. `func Vec[T]::get[U type](self *this, value U) U` is that template on a generic struct. The call is `value.get[i32](value)`. `T` comes from the struct. `U` is chosen at the call. A call without the brackets is `undeclared identifier`. A type parameter that repeats a struct parameter is `redefinition`. `export` and a static method on that template are `not supported yet`. `extern func Type::name[T type]` names that template. The call stays `value.name[i32](...)`. Each specialization is its own symbol. A body defines that symbol. `extern(abi="c")` on that template is reported as `extern(abi="c") on a trait or a generic`. A prototype with no body is reported as `not supported yet`. A default method is the same form on a trait name, with a body: `func Shape::twice(self *this) i32 { ... }`. `extern func Shape::twice` names that default. The call is `s.twice()` when `s` is `*Shape`, and `r.twice()` when `r` gained the default. A struct method with the same parameters wins. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are reported as `extern(abi="c") on a trait or a generic`. `extern(name="symbol")` is reported as `name requires abi="c"`. A prototype with no body is reported as `not supported yet`. The method inside the trait braces has no body and no receiver. A `{` body is reported as `expected ';'`.

### Generics

`func name[T type, N i32](value T) [N]T` is a generic function. `T type` is a type. `N i32`, or another integer type, is a compile-time integer. `T Shape` requires a type that has `Shape`'s methods. A parameter `*T` accepts a value receiver and a pointer receiver. A parameter `T` requires a value receiver. `[...U]` is not a type parameter. `...` in that list is `expected an identifier`. `...` is the C argument list on `extern(abi="c")`. `T` is the type of the value, including a tuple and a struct. `id((40, 2))` infers that tuple. A parameter written as a tuple, `other (P1, P2)` in `func SomeFunc[P1, P2](value i32, other (P1, P2))`, is `expected a type`. `[P1, P2]` is `expected a type` on the comma. With `[P1 type, P2 type]`, the `(` of `(P1, P2)` is `expected a type`. The same parameter on `func Point::take` and `func Vec[T]::take` is that error. `func take(other (i32, f32))` is that error too. `id(Point{.x = 40, .y = 2})` infers that struct. The call `id(1)` infers `T`. `repeat[4](7)` writes `N` and infers `T`. `id[i32](1)` and `zero[i32]()` write a type. `extern(abi="c")` on a generic function, on a function whose parameter is a trait, or on `type Name[T] struct` is reported as `extern(abi="c") on a trait or a generic`. `extern` and `extern(abi="tlc")` on a generic function name that function in a tlc library. The call stays `id(value)` or `id[i32](value)`. Each specialization is its own symbol. A body defines that symbol. A call of a generic that has no body is reported as `not supported yet`. `...` on a generic function stays rejected and is reported as `not supported yet`. `...` is only legal on `extern(abi="c")`. A method on a generic struct names that struct's parameters, `func Vec[T]::get(self *this) T`. A static function there is `func Vec[T]::make(value T) Vec[T]`, called as `Vec[i32].make(value)`. After the call is specialized, `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` use the concrete type and the integer. A generic body may omit the result. The type comes from its `return` expressions.

## Variables

`var n i32 = 1` stores 1: 1 instruction. A file-scope integer `const` folds into the instruction that uses it and adds no load. The sequences are on [Bindings](statements/binding.md).

A typed variable names the type in Go order, after the name:

```ebnf
var-decl    = "var" identifier go-type [ "=" expression ] ";"
            | "var" identifier "=" expression ";"
            | "var" identifier [ "[" name { "," name } "]" ] "::" identifier [ go-type ] "=" expression ";"
            | "var" identifier [ "[" name { "," name } "]" ] "::" identifier go-type [ "=" expression ] ";"
            | identifier ":=" expression ";" ;
const-decl  = "const" identifier [ go-type ] "=" expression ";"
            | "const" identifier [ "[" name { "," name } "]" ] "::" identifier [ go-type ] "=" expression ";" ;
```

`var Entity::next i32 = 1` belongs to the struct or enum `Entity`. It is not a field. On a generic struct the names match the struct, as in `var Box[T]::extra i32` and `var Buf[N, T]::count i32 = N`. The use is `Entity.next`, `Box[i32].extra`, or `Buf[4, i32].count`. `export var Box[T]::extra` publishes that variable. Another package reads and assigns `pkg.Box[i32].extra`. `const Entity::next = 1` and `const Box[T]::limit = 4` are constants on the type, written beside it. See [Variables on a type](methods/variable.md) and [Constants on a type](methods/constant.md).

`var name :=` is an error. Inside a function, several names unpack one tuple. A call that returns several results is that tuple:

```ebnf
binding     = "var" name-list "=" expression ";"
            | name-list ":=" expression ";"
            | name-list "=" expression ";" ;
```

`name :=` and `var` each define a binding. A later one in the same block hides the earlier binding. The initializer sees the previous binding. `=` assigns names that already exist. A second `const` in the block is `redefinition`. Two of the same name in one `var` or `:=` are `redefinition`.

A variable whose type is a function pointer is a `var` whose type is a `func-type`, as in `var fp func (i32, i32) i32 = add`. `var name = function` infers that pointer from a function or a static function.

## Structs, enums, traits, and type aliases

Declaring a type adds no instructions. A value of the type costs what [Structs](types/struct.md), [Unions](types/union.md), [Enums](types/enum.md), and [Traits](methods/trait.md) measure. A trait parameter is specialized, so the call does not consult a table.

```ebnf
type-decl   = "type" identifier [ type-params ] type-body ;
type-body   = "struct" [ align ] ( "{" { field } "}" [ ";" ] | ";" )
            | "enum" [ "(" underlying ")" ] "{" [ enumerators ] "}" [ ";" ]
            | "trait" "{" { trait-item } "}" [ ";" ]
            | "union" "(" identifier [ "." identifier ] ")" "{" { union-variant } "}" [ ";" ]
            | "union" "{" field { field } "}" [ ";" ]
            | go-type ";" ;
union-variant = identifier "struct" "{" { field } "}" ";"
              | field ;
align       = "(" "align" "=" integer-literal ")" ;
field       = [ "export" ] identifier go-type [ "=" expression ] ";"
            | [ "export" ] identifier [ "export" ] "struct" "{" { field } "}" ";"
            | [ "export" ] identifier [ "export" ] "union" "{" { field } "}" ";"
            | [ "export" ] identifier [ "export" ] "union" "(" identifier [ "." identifier ] ")" "{" { union-variant } "}" ";" ;
declarator  = identifier [ array ]
            | "(" "*" { "*" } identifier ")" "(" [ func-args ] ")" ;

underlying  = integer-name | "bool" ;
enumerators = enumerator { "," enumerator } [ "," ] ;
enumerator  = identifier [ "=" integer-literal ] ;

trait-item   = trait-method | field ;
trait-method = [ "unsafe" ] "func" identifier "(" [ parameters ] ")" result ";" ;
```

`type Vec[T type] struct` is a generic struct. The parameter list is allowed before `struct`. One `name type` is also allowed before `trait`. A use is `Vec[i32]`, `Vec[Point]`, or, in another package, `pkg.Vec[i32]`. A struct value is `Vec[i32]{}`, `Buf[4, i32]{}`, `pkg.Vec[i32]{}`, and `pkg.Buf[4, i32]{}`. A method on `Vec` is `func Vec[T]::get(self *this, i i32) T`. One method name may have several parameter lists. The receiver is not one of those parameters. The names are the struct's parameters, in that order. Leaving them out, or writing a different name, is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. `Vec` without arguments is reported as `type mismatch`. `Vec[i32]` and `Vec[u8]` are different types. Assigning one to the other, or passing one where the parameter is the other, is reported as `type mismatch`.

The semicolon after `}` is optional. A member is a name and then a `go-type`, the same order as `var`: `x i32`, `next *Node`, `raw [8]u8`, `op func (i32, i32) i32`. `kind const Type` stores a constant value. Assigning that field is `type mismatch`. `const` before a type on a variable, a parameter, or a typedef is `not supported yet`. It still ends with `;`. `x i32 = 1` is the value of a field that an initializer does not name. The value is a constant, the same kind a file-scope `const` accepts. A call is reported as `global initializer is not a constant`. `raw [2]i32 = {1, 2}` is that value for a sized array. A short list leaves the elements it does not name uninitialized. `0...` stores 0 in those elements. A file-scope variable stores 0 in an element the list does not name. Naming the field replaces the list. A field of an inline union payload uses that same value, as in `n i32 = 1` inside `union(Enum)`. A union with no initializer does not choose an arm. A value on a trait field, `w i32 = 1`, is reported as `expected ';'`. `export x i32` publishes that member. `var` and `const` inside the struct are reported as `not supported yet`. A variable for the type is `var Type::name` beside the struct. `pkg.Box[i32].name` and `pkg.Buf[4, i32].name` read an exported constant on a generic struct. That constant is `const Type::name` beside the struct. `export var Box[T]::extra` publishes the variable the same way. Another package reads and assigns `pkg.Box[i32].extra`. An inline struct is `body struct { k i32; }`, `body export struct { k i32; }`, or `export body struct { k i32; }`. An inline union is `data union { n i32; m i32; }`, `data export union { n i32; m i32; }`, or `export data union { n i32; m i32; }`. `value union(Kind) { A struct { n i32; }; B struct { n i32; }; }` is that field when the union is tagged. Exporting the member exports every member inside it. A value on an untagged union field, `n i32 = 4`, is reported as `not supported yet`. `export k i32` there is allowed and changes nothing. `type Name struct(align=16)` sets the alignment. The literal is a positive power of two, at most 4096. A weaker alignment than the fields require is reported as `type mismatch`. `type Name struct;` is an empty struct, the same type as `type Name struct {}`. `sizeof` is 0, and the struct can have methods. `type Name struct(align=16);` is that empty struct with the alignment, and `sizeof` is still 0. `type Name struct` with neither `{` nor `;` is reported as `expected '{'`. An enum, a trait, and a union still need `{`. `extern type Name struct;` has the same size as `extern type Name struct {}`. An empty `extern(abi="c") struct` is written `extern(abi="c") type Name struct;` or `extern(abi="c") type Name struct { }` and occupies one byte.

### Unions, enums, and traits

`type Name union(Enum)` is a tlc union. `Enum` is an enum in the same package, or `pkg.Enum` when that package is imported and the enum is exported. A qualifier that is not an imported package is reported as `undeclared identifier`. A name that is not exported is reported as `Enum is not exported`. A name that is not an enum is reported as `type mismatch`. `import { Enum }` does not bind the package name. Each enumerator is one field, and each field names one enumerator. A field that is not an enumerator is reported as `undeclared identifier`. An enumerator with no field is reported as `type mismatch`. The field is an inline struct, `Circle struct { radius f32; };`, or the name of a struct, `Circle Circle;`. A field that is not a struct is reported as `type mismatch`. The value stores one payload and the enum tag after it. Every field starts at offset 0. The payload is large enough for the largest field, and the tag follows it. Assigning a field, or a field inside it, stores that enumerator. A designated initializer does the same. `Enum(value)` reads the tag. `pkg.Enum(value)` reads it when the union names that enum. Reading a field does not check the tag.

`extern(abi="c") type Name union` is a C union. It does not name an enum. `extern(abi="c") type Name union(Enum)` is reported as `extern(abi="c") on a tagged union`. `extern type Name union(Enum)` keeps the tag. Every field starts at offset 0, the size is the largest field, and the alignment is the strictest field.

`extern(abi="c") type Name enum` has no underlying type. `extern(abi="c") type Name enum(i8)` is reported as `extern enum`. An enumerator value is a decimal integer literal with no suffix, or with the suffix `i32`.

`type Seq[T type] trait` is a generic trait. `T` may be a method parameter, a method result, or a field. `Seq[i32]` and `Seq[u8]` are different traits. A parameter is `items Seq[i32]` or `items *Seq[i32]`. `Seq` without arguments is reported as `type mismatch`. The wrong number of arguments is reported as `argument count`. An integer parameter, a pack, several parameters, and `trait(Parent)` on that list are reported as `not supported yet`. `func Seq::name` is reported as `not supported yet`. `export` publishes that trait. Another package names `pkg.Seq[i32]` after `import`. `extern` and `extern(abi="tlc")` are accepted on that trait. The trait has no size and no symbol. `extern(abi="c")` and `extern(abi="c", name="symbol")` are reported as `extern(abi="c") on a trait or a generic`. A result `Seq[i32]`, `var s Seq[i32]`, an array, a slice, and a field of the trait are reported as `trait has no size`. `import "std/seq"` names the iteration trait in `lib/tlc/std/seq`. That package's `link` file names `libseq.a`. The compiler reads `len` and `at` from that declaration. `seq.Seq[T]` and `*seq.Seq[T]` check those two methods. A pointer to an element is `ptr_of`, which has `len` and `ref`. A field, a further method, or a default on that trait is reported as `not supported yet`. A copy of the trait that does not link that library is an ordinary trait. An array `[N]T` and a slice `[]T` satisfy the iteration trait when the element of an index is the trait's type argument. `len` is `.len`, `at` is the index, and `ref` is `&items[i]`. A slice alias satisfies it only by its methods. A pointer to an array or a slice is reported as `type mismatch`.

`type Name trait(Parent)` copies `Parent` into `Name` while compiling. A `{` body is reported as `expected ';'`. A trait method has no body and no receiver. A parameter is `Shape` or `*Shape`. `export func use(s Shape)` and `export func Rect::paint(self *this, s Shape)` publish that function when the ABI is not C. Another package calls `pkg.use(r)` or `value.paint(s)`. The struct's method is exported when the function and the struct are in different packages, or the call is `area is not exported`. `extern(abi="c")` on that function is `extern(abi="c") on a trait or a generic`. A tlc `extern` names that specialization. The call stays `use(r)` or `value.paint(s)`. Each struct is its own symbol. A body defines that symbol. A prototype may be the declaration that says `extern`. A call that specializes a prototype with no body is `not supported yet`. `...` is `not supported yet`. `var s Shape`, `var s *Shape`, `func give() Shape`, `func give() *Shape`, `[2]Shape`, `[]Shape`, and a field `item Shape` in a struct or a trait are reported as `trait has no size`. It may return several results, `(i32, i32)`, and the call unpacks them the same way a function call does. `unsafe func draw() void;` makes that method unsafe. The call is `unsafe s.draw()`. A default method is written on the trait name, outside the braces, and has a body. The call is `s.twice()` when `s` is `*Shape`, and `r.twice()` when `r` gained the default. A missing method that the trait requires is reported as `undeclared identifier`. A trait field is a name and a type, the same form as a struct field: `w i32;`. A value on that field, `w i32 = 1`, is reported as `not supported yet`. The struct that is passed must have a field of that name and that type.

## Types

A type spelling adds no instructions. `sizeof` and `alignof` fold to an immediate. `alignof` is the alignment in bytes. The widths and the conversions are on [Numeric types](types/integer.md).

A pointer is a leading star in every type position: `*i32`, `?*Point`, `*pkg.Point`, `*const u8`, `type Name *i32`, `sizeof(*i32)`, and `*i32(expr)`. `?` is written immediately before `*`. `*T` has a value. `?*T` may be `null`. `i32 *`, `Point *`, `sizeof(i32 *)`, and `i32*(expr)` are reported as `type mismatch`. An array is written in front of its element type in every position: `[4]i32`, `[N]i32`, `type Pair [2]i32`, and `func head(a [4]i32)`. An empty length is a slice: `[]i32` and `func head(a []i32) []i32`. One more empty pair is a slice of those headers: `[][]i32`, `[N][]i32`, and `func sum(rows [][]i32) [][]i32`. `const` before the element makes that element constant: `[]const u8`, `[4]const u8`, and `[][]const u8`. A string literal is `[]const u8`. `i32[4]`, `i32[N]`, and `i32[]` are reported as `type mismatch`. Another pair is the same constructor again: `[N][M]T`, `[][N]T`, and `[][][]T`. Each empty pair is one slice header. Each length is one array of the inner type.

```ebnf
go-type     = func-type
            | { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
            | "[" array-length "]" [ "[]" ] { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
            | [ "?" ] "*" [ "const" ] "[" array-length "]" { [ "?" ] "*" [ "const" ] } base-type [ type-args ] ;

placed-type = func-type
            | { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
            | go-type ;

array-length = [ integer-literal | identifier ] ;
type-args   = "[" type-arg { "," type-arg } "]" ;
type-arg    = integer-literal
            | "[" array-length "]" { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
            | { [ "?" ] "*" } base-type [ type-args ]
            | array-length ;

func-type   = "func" "(" [ func-args ] ")" result ;
func-args   = placed-type { "," placed-type } ;

base-type   = "void" | "bool"
            | integer-name | float-name
            | identifier
            | identifier "." identifier ;

integer-name = "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "i64" | "u64" ;
float-name   = "f32" | "f64" ;
```

A `*` before `[` is a pointer to that array or slice, as in `*[4]i32`, `**[4]i32`, `?*[4]i32`, `*[]T`, `*[][]T`, and `*[N][]T`. `*const [4]i32` is a pointer to a constant array. A store through it is reported as `type mismatch`. `*const T` is a pointer to constant `T`. A store through it is reported as `type mismatch`. `*T` may be passed where `*const T` is required. A type argument may be a nested slice, as in `Vec[[][]i32]`. `*T` when `T` is `[4]i32` is a pointer to that array.

`int`, `uint`, `short`, `long`, `ulong`, `char`, `float`, and `double` are ordinary identifiers. Using one where a type is required is reported as `not supported yet`. `signed` and `unsigned` are reported as `type mismatch`.

An array uses `go-type` in every position, including a parameter and a result: `[4]i32` and `[N]i32`. `i32[4]` and `i32[]` are reported as `type mismatch`. A type alias uses the same order as `var`: `type Integer i32`, `type Ptr *Point`, `type Pair [2]i32`, `type BinOp func (i32, i32) i32`. `type Something (i32, f32)` is `expected a type` on `(`. `type Something[K,V] (K,V)` is `expected ']'` on the comma. The alias is a distinct type. A cast such as `Integer(n)` or `i32(value)` confirms the type. A file-scope constant can name a struct, a function pointer, or a typedef: `const value Integer = Integer(10)` and `const value = Integer(10)`. The function-pointer declarator `i32 (*name)(` parameters `)` still parses. A new variable is not written as a C declarator. `i32 value = 1` is reported as `expected var`.

## Statements

`if (n != 0)` is a compare and a jump. `while` and `for` repeat that compare. `switch` subtracts and branches. `defer` is a flag and a call without a trace frame. `assert` adds no instructions. `emit` is only valid inside a `macro`. A `for` there unrolls, and an `if` there keeps one arm and adds no branch. A macro that stores 40 when its `comptime` argument is 1, and 2 otherwise, leaves 3 instructions in `main`: the store of 40, the load, and `retq`. The counts are on the statement pages.

```ebnf
block       = "{" { statement } "}" ;
statement   = block
            | if-statement
            | "while" "(" expression ")" block
            | "do" statement "while" "(" expression ")" ";"
            | "for" "(" for-init [ expression ] ";" [ expression ] ")" block
            | "for" "(" ( "var" | "const" ) identifier ":" expression ")" block
            | "switch" "(" expression ")" statement
            | "case" expression ":" statement
            | "default" ":" statement
            | "return" [ expression { "," expression } ] ";"
            | "break" ";"
            | "continue" ";"
            | "defer" expression ";"
            | "emit" expression ";"
            | "emit" "for" "(" for-init [ expression ] ";" [ expression ] ")" block
            | "emit" "for" "(" ( "var" | "const" ) identifier ":" expression ")" block
            | "emit" "if" "(" expression ")" block [ "else" ( if-statement | block ) ]
            | "emit" "while" "(" expression ")" block
            | "emit" "do" statement "while" "(" expression ")" ";"
            | assert-stmt
            | var-decl
            | binding
            | function
            | expression ";"
            | ";" ;

assert-stmt = "assert" "(" expression "," string ")" ";" ;
test-decl   = "test" string block ;

if-statement = "if" "(" expression ")" block [ "else" ( if-statement | block ) ] ;
for-init    = ";" | var-decl | binding | expression ";" ;
```

`if`, `while`, and `for` take a block. `else` takes a block or another `if`. A body without `{` is reported as `expected '{'`. `defer` takes one call expression. `assert` checks a constant `bool` at compile time. The string is the diagnostic when the condition is false. `case` and `default` are statements, so they appear inside the switch body.

## Expressions

An `i32` add of two locals is 2 instructions. A comparison is 4 and yields `bool`. A traced call is 36 instructions in the caller when fewer than 32 frames are stored. The operator table is on [Expressions](expressions/operator.md).

Operators bind from the top of this list downward. Assignment is right-associative. The others are left-associative.

```ebnf
expression  = or [ "=" expression ] ;
or          = and { "||" and } ;
and         = bit-or { "&&" bit-or } ;
bit-or      = bit-xor { "|" bit-xor } ;
bit-xor     = bit-and { "^" bit-and } ;
bit-and     = equality { "&" equality } ;
equality    = relational { ( "==" | "!=" ) relational } ;
relational  = shift { ( "<" | ">" | "<=" | ">=" ) shift } ;
shift       = additive { ( "<<" | ">>" ) additive } ;
additive    = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
unary       = "unsafe" unary
            | new-expr
            | delete-expr
            | ( "+" | "-" | "!" | "~" | "&" | "*" | "sizeof" | "typeof" ) unary
            | postfix ;
postfix     = primary { postfix-op } ;
postfix-op  = "(" [ argument-list ] ")"
            | type-args "(" [ argument-list ] ")"
            | "." identifier
            | "[" expression "]"
            | "[" [ expression ] ":" [ expression ] "]"
            | "!"
            | struct-init ;
argument-list = expression { "," expression } ;
new-expr    = "new" [ "(" expression ")" ] [ new-count ] { [ "?" ] "*" [ "const" ] } [ identifier "." ] identifier [ type-args ] [ new-init ] ;
new-count   = "[" expression "]" | "[" "_" "]" ;
new-init    = struct-init | element-list ;
element-list = "{" [ element { "," element } [ "," ] ] "}" ;
element     = expression [ "..." ] ;
delete-expr = "delete" [ "(" expression ")" ] unary ;
```

`name[args](` is a generic call, not an index. An array length may be a file-scope `const` name: `[N]i32`.

Postfix `!` asserts that a `?*T` has a value. `pointer!` has type `*T`. `pointer!.value` reads the field. Prefix `!` is logical not on a `bool` and yields `bool`. An integer is reported as `type mismatch: expected bool, found i32`. A null pointer calls `builtin.panic("null pointer")`.

`a[i:j]` is the slice of elements from `i` up to, but not including, `j`. A missing low bound is `0`. A missing high bound is the length. `a[i:]`, `a[:j]`, and `a[:]` are those slices. `a[]` is reported as `expected an expression`.

```ebnf
primary     = identifier
            | integer-literal
            | float-literal
            | string
            | char-literal
            | "true" | "false" | "null"
            | "(" expression ")"
            | "(" expression "," expression { "," expression } ")"
            | anonymous
            | short-function
            | cast
            | struct-init
            | array-literal ;

array-literal = "[" "_" "]" go-type struct-init ;

struct-init = "{" [ designator { "," designator } [ "," ] ] "}"
            | "{" expression { "," expression } [ "," ] "}" ;
designator  = "." identifier "=" expression ;

anonymous   = "func" "(" [ parameters ] ")" [ result ] block ;
short-function = "(" [ name-list ] ")" block ;
cast        = { "*" } scalar-type "(" expression ")"
            | { "*" } identifier "(" expression ")"
            | func-type "(" expression ")" ;
scalar-type = "void" | "bool" | integer-name | float-name ;
```

An anonymous function with no result type has result `void`. The short form is accepted where a function pointer is expected, and as a call argument whose type is a trait with one method. Its names and its result come from that type. A named function with those parameters and that result is accepted as that argument. `pkg.name` is that function when it is exported. `Type.name` is that function when it is static. A cast writes the type, then `(`, as in `i32(n)`, `*i32(p)`, `?*i32(p)`, or `func (i32) i32(p)`. A built-in name such as `i32` is always that cast. A name declared as a type in this file is a cast too: `Integer(n)`, `*Point(p)`, `ShapeType(shape)`. A cast to a struct is reported as `type mismatch`. `ShapeType(shape)` reads the tag of a tlc union, including from another file in the package. The cast has the type that was written, so `*i32(p)` is `*i32` and `?*i32(null)` is `?*i32`. `*i32(null)` is reported as `type mismatch`. A pointer cast that changes the claim is [`unsafe`](expressions/unsafe.md) and is reported as `unsafe cast` without that prefix. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type. `i32*(p)` is reported as `type mismatch`. `sizeof` of a parenthesized type is `sizeof(i32)`, `sizeof(*i32)`, `sizeof(enum Color)`, or `sizeof` of a declared name such as `sizeof(Point)`. `sizeof(struct Point)` is reported as `expected a type`. `sizeof(*p)` is the size of the dereferenced value when `p` is a variable. `sizeof` of a value is `sizeof` followed by a unary expression. `typeof` has the same shape and produces a `Type`. `typeof(i32)`, `typeof(*i32)`, and `typeof n` are accepted. `typeof(struct Point)` is reported as `expected a type`.

A struct initializer names fields: `{.value = 10, .complex = {.inner_value = 20}}`. The struct type comes from the destination. A field that is left out takes the value written on the field, and stays uninitialized when the field has none. `0...` stores 0 in every value the list does not name. A trailing comma is allowed. `{10, 20}` is a value list. Where the destination is an array, it fills that array. Where a struct is expected, the value list is reported as `type mismatch`. An empty `{}` names no fields. Mixing a named field and a positional value is reported as `type mismatch`. `(40, 2)` is a tuple. `(40)` is the value `40`. `var value = (40, 2)` and `value := (40, 2)` bind that tuple. `var value1, value2 = value` and `a, b := value` unpack it. `value[0]` reads an element.

`struct-init` after a name, or after `name.name`, is a struct value: `Point{}`, `Point{.x = 1, .y = 2}`, and `geom.Point{}`. A type application in that position is the same value: `Vec[i32]{}`, `Vec[i32]{.n = 1}`, `pkg.Vec[i32]{}`, `Buf[4, i32]{}`, and `pkg.Buf[4, i32]{}`. The type is that struct. Fields that are left out take the value written on the field, and stay uninitialized when the field has none. `var p = Point{}`, `const origin = Point{}`, and `p := Point{}` take the type from the name. `Point{1, 2}` and `Vec[i32]{1, 2}` are value lists and are reported as `type mismatch`. A name that is not a struct is reported as `type mismatch`. A type application that is not a generic struct is reported as `type mismatch`. The wrong number of type arguments is reported as `argument count`. A generic struct that is not exported is reported as `name is not exported`. A `{` after a call, an index, or `value.field` is not this form. `new Point{.x = 1}` and `new(&arena) Point{.x = 1}` reserve a `Point` and write those fields. The allocator argument is a pointer, and the pointee type is taken from that pointer. `new(arena)` is reported as `type mismatch: expected *Arena, found Arena`. A field that is left out takes the value written on the field, and stays uninitialized when the field has none. `0...` stores 0 in every value the list does not name. `new i32` and `new [n]i32` do not store the reserved memory. A `*T` field has to be named, or the initializer is reported as `type mismatch`, unless the field itself has a value. `new Point{1, 2}` and `new i32{1}` are reported as `type mismatch`. `new [n]T` reserves `n` elements and yields `*T`. `n` has type `i32`. `new [n]Point{{.x = 1}...}` stores that value in every element. `new [n]Point{.x = 1}` is parsed as a field list and is reported as `type mismatch`. `new [_]Point{{.x = 1}, {.y = 2}}` takes the length from the list, so the object is `[2]Point` and the result is `*[2]Point`. `new Box[i32]` allocates that specialization, and `new Box[i32]{.value = 1}` writes its fields. `new []T` is reported as `expected an expression`.

`[_]i32{10, 20, 12}` is an array literal. `_` stands for the length, which is the number of values, so the type is `[3]i32`. At file scope it initializes a variable when each value is a constant: `var row = [_]i32{10, 20, 12}`, `var grid = [_][2]i32{{1, 2}, {3, 4}}`, `var deep = [_][2][3]i32{{{1, 2, 3}, {4, 5, 6}}}`, and `const fixed = [_]i32{19, 2}`.

## Tokens

A token adds no instructions. Comments are discarded by the lexer.

```ebnf
identifier  = ( letter | "_" ) { letter | digit | "_" } ;
letter      = "A" … "Z" | "a" … "z" ;
digit       = "0" … "9" ;
hex-digit   = digit | "a" … "f" | "A" … "F" ;
octal-digit = "0" … "7" ;
bin-digit   = "0" | "1" ;

integer-literal = hex | binary | octal | decimal ;
decimal     = digit { digit | "'" digit } [ int-suffix ] ;
hex         = "0" ( "x" | "X" ) hex-digit { hex-digit | "'" hex-digit } [ int-suffix ] ;
octal       = "0" octal-digit { octal-digit | "'" octal-digit } [ int-suffix ] ;
binary      = "0" ( "b" | "B" ) bin-digit { bin-digit | "'" bin-digit } [ int-suffix ] ;
int-suffix  = "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "i64" | "u64" ;

float-literal = decimal-float | hex-float ;
decimal-float = digits "." [ digits ] [ exponent ] [ float-suffix ]
              | digits exponent [ float-suffix ]
              | "." digits [ exponent ] [ float-suffix ] ;
hex-float   = "0" ( "x" | "X" ) hex-digits [ "." [ hex-digits ] ]
              binary-exponent [ float-suffix ] ;
exponent    = ( "e" | "E" ) [ "+" | "-" ] digits ;
binary-exponent = ( "p" | "P" ) [ "+" | "-" ] digits ;
float-suffix = "f" | "F" ;
digits      = digit { digit | "'" digit } ;
hex-digits  = hex-digit { hex-digit | "'" hex-digit } ;

string      = '"' { string-char } '"'
            | "`" { raw-char } "`" ;
char-literal = "'" ( char | escape ) "'" ;
escape      = "\" ( "'" | '"' | "?" | "\" | "a" | "b" | "f" | "n" | "r" | "t" | "v"
            | octal-digit [ octal-digit [ octal-digit ] ]
            | "x" hex-digit { hex-digit } ) ;
```

A `'` between digits is a separator, not an empty gap and not a trailing mark. A quoted string cannot cross a line. A raw string, between backticks, can. In a raw string a backslash is a character, and a carriage return before a line feed is dropped. `f` or `F` makes a float literal `f32`. There is no `l` suffix.

These words are keywords and cannot be identifiers:

```text
alignof assert bool break case comptime const continue default defer delete do else emit enum
export extern false for func if import macro new null package
return sizeof struct switch test trait true type typeof union unsafe var void while
```

C keywords that tlc reserves but does not use are also keywords: `alignas`, `auto`, `constexpr`, `goto`, `inline`, `register`, `restrict`, `static_assert`, `thread_local`, `typeof_unqual`, `volatile`, and the spellings that start with `_` (`_Alignas`, `_Bool`, `_Complex`, and the rest of that set).

`as`, `from`, `impl`, `static`, and `this` are not keywords. `signed` and `unsigned` are not keywords.
