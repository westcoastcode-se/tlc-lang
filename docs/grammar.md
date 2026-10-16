# Grammar

[Index](README.md)

This page is the syntax of tlc. Meaning, types, and diagnostics are in the [language reference](README.md). A form that this grammar does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

## Notation

The grammar is EBNF. A name is a rule. `"text"` is a terminal. `[ x ]` is optional, `{ x }` is zero or more, and `( x )` groups. `|` separates alternatives. When two alternatives could match, the parser tries the earlier one.

Spaces, tabs, newlines, `//` comments, and `/* */` comments are ignored between tokens.

## Source file

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

`export` and `extern` may stand, in any order, in front of a function, a method, a variable, or a `type`. `export` may also stand in front of a file-scope `const`, in front of `const Type::name`, in front of `var Type::name`, in front of `var name = expr`, and in front of `name := expr`. `export const Type::name` publishes that constant, including on a generic struct. Another package reads `pkg.Type.name` or `pkg.Box[i32].name`. A `const` inside a struct is reported as `not supported yet`. `extern` on `const Type::name`, and `extern` on `var name = expr` or `name := expr`, are reported as `not supported yet`. `extern` may be followed by `(abi="c")` or `(abi="tlc")`. A bare `extern` means `abi="tlc"`. `extern(abi="c")` names a C library symbol and is linked only together with a C library (`-l` or a library file). `extern(abi="tlc")` names a tlc library symbol.

## Functions and methods

```ebnf
function    = "func" identifier [ type-params ] "(" [ parameters ] ")" result ( block | ";" ) ;
type-params = "[" type-param { "," type-param } "]"
            | "[" "..." identifier "]" ;
type-param  = identifier "type"
            | identifier integer-name
            | identifier identifier ;
method      = "func" identifier [ method-params ] "::" identifier "(" [ parameters ] ")" result ( block | ";" ) ;
method-params = "[" identifier { "," identifier } "]" ;
receiver    = identifier [ "*" ] "this" ;

parameters  = parameter { "," parameter } [ "," "..." ] ;
parameter   = identifier placed-type ;

result      = placed-type
            | "typeof" unary
            | "(" placed-type { "," placed-type } ")" ;
```

A function with no body is a prototype and ends with `;`. A body replaces that semicolon. `...` is allowed on an `extern(abi="c")` declaration and on a function with a body. Inside that body, `builtin.va()` reads the extra arguments. One parenthesized result is the same type as the bare result. `()` as a result list is an error. A result may be a function pointer: `func pick() func (i32, i32) i32`. `pick()` is that pointer, and `pick()(1, 2)` calls it. A method uses the same result. The returned value may be a function name or an anonymous function, `return (a, b) { return a + b; }` or `return func (a i32, b i32) i32 { return a + b; }`. An inner function called directly may return a function pointer taken from an enclosing function.

### Methods

A receiver parameter is written `name this`, `name *this`, `name *const this`, `name ?*this`, or `name ?*const this`. `this` stands for the method's struct or enum, so a pointer is `*this` and an optional pointer is `?*this`. `*const this` is a pointer to a constant value. A const variable cannot call `*this` or `?*this`. Those calls are reported as `type mismatch: expected *Point, found *const Point` and `type mismatch: expected ?*Point, found *const Point`. `this` is an ordinary identifier everywhere else. When the parameter list has no receiver, the function is static and the call is `Type.name(arguments)`. On a generic struct the call is `Vec[i32].name(arguments)` or `Buf[4, i32].name()`.

The method is written in the same file as that struct or enum. A method in another file is reported as `method in another file`. Every file in the package can call it.

An inner function uses the same `function` form inside a block, and it has a body.

### Generics

`func name[T type, N i32](value T) [N]T` is a generic function. `T type` is a type. `N i32`, or another integer type, is a compile-time integer. `T Shape` requires a type that has `Shape`'s methods. `[...U]` is a type pack and is the last parameter in its brackets. A pack before another parameter is reported as `not supported yet`. One argument type makes `U` that type. Several make `U` a tuple. `func head[T type, ...U](value T, rest U) T` places the pack last. The call `id(1)` infers `T`. `repeat[4](7)` writes `N` and infers `T`. `id[i32](1)` and `zero[i32]()` write a type. `keep[i32, u8]((40, 'a'))` writes the tuple types. A method on a generic struct names that struct's parameters, `func Vec[T]::get(self *this) T`. A static function there is `func Vec[T]::make(value T) Vec[T]`, called as `Vec[i32].make(value)`. After the call is specialized, `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` use the concrete type and the integer. A bare result may be `typeof(expression)`, checked once the parameters exist.

## Variables

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

`var Entity::next i32 = 1` belongs to the struct or enum `Entity`. It is not a field. On a generic struct the names match the struct, as in `var Box[T]::extra i32` and `var Buf[N, T]::count i32 = N`. The use is `Entity.next`, `Box[i32].extra`, or `Buf[4, i32].count`. `const Entity::next = 1` and `const Box[T]::limit = 4` are constants on the type, written beside it. See [Methods](methods.md).

`var name :=` is an error. Inside a function, several names unpack one tuple. A call that returns several results is that tuple:

```ebnf
binding     = "var" name-list "=" expression ";"
            | name-list ":=" expression ";"
            | name-list "=" expression ";" ;
```

`name :=` defines a name that is new in the block and assigns a name that already exists there. At least one name must be new. A `:=` whose names are all already in the block is `type mismatch`. `=` assigns names that already exist. `var` defines every name.

A variable whose type is a function pointer is a `var` whose type is a `func-type`, as in `var fp func (i32, i32) i32 = add`. `var name = function` infers that pointer from a function or a static function.

## Structs, enums, traits, and type aliases

```ebnf
type-decl   = "type" identifier [ type-params ] type-body ;
type-body   = "struct" [ align ] ( "{" { field } "}" [ ";" ] | ";" )
            | "enum" [ "(" underlying ")" ] "{" [ enumerators ] "}" [ ";" ]
            | "trait" "{" { trait-item } "}" [ ";" ]
            | "union" "(" identifier ")" "{" { union-variant } "}" [ ";" ]
            | "union" "{" field { field } "}" [ ";" ]
            | go-type ";" ;
union-variant = identifier "struct" "{" { field } "}" ";"
              | field ;
align       = "(" "align" "=" integer-literal ")" ;
field       = [ "export" ] identifier go-type ";"
            | identifier [ "export" ] "struct" "{" { field } "}" ";" ;
declarator  = identifier [ array ]
            | "(" "*" { "*" } identifier ")" "(" [ func-args ] ")" ;

underlying  = integer-name | "bool" ;
enumerators = enumerator { "," enumerator } [ "," ] ;
enumerator  = identifier [ "=" integer-literal ] ;

trait-item   = trait-method | field ;
trait-method = "func" identifier "(" [ parameters ] ")" result ";" ;
```

`type Vec[T type] struct` is a generic struct. The parameter list is allowed only before `struct`. A use is `Vec[i32]` or `Vec[Point]`. A method on `Vec` is `func Vec[T]::get(self *this, i i32) T`. The names are the struct's parameters, in that order. Leaving them out, or writing a different name, is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. `Vec` without arguments is reported as `type mismatch`. `Vec[i32]` and `Vec[u8]` are different types. Assigning one to the other, or passing one where the parameter is the other, is reported as `type mismatch`.

The semicolon after `}` is optional. A member is a name and then a `go-type`, the same order as `var`: `x i32`, `next *Node`, `raw [8]u8`, `op func (i32, i32) i32`. It still ends with `;`. `export x i32` publishes that member. `var` and `const` inside the struct are reported as `not supported yet`. A variable for the type is `var Type::name` beside the struct. `pkg.Box[i32].name` and `pkg.Buf[4, i32].name` read an exported constant on a generic struct. That constant is `const Type::name` beside the struct. An inline struct is `body struct { k i32; }` or `export body struct { k i32; }`. Exporting that member exports every member inside it. `export k i32` there is allowed and changes nothing. `type Name struct(align=16)` sets the alignment. The literal is a positive power of two, at most 4096. A weaker alignment than the fields require is reported as `type mismatch`. `type Name struct;` is an empty struct, the same type as `type Name struct {}`. `sizeof` is 0, and the struct can have methods. `type Name struct(align=16);` is that empty struct with the alignment, and `sizeof` is still 0. `type Name struct` with neither `{` nor `;` is reported as `expected '{'`. An enum, a trait, and a union still need `{`. `extern type Name struct;` has the same size as `extern type Name struct {}`. An empty `extern(abi="c") struct` is written `extern(abi="c") type Name struct;` or `extern(abi="c") type Name struct { }` and occupies one byte.

### Unions, enums, and traits

`type Name union(Enum)` is a tlc union. `Enum` is an enum in the same package. Each enumerator is one field, and each field names one enumerator. A field that is not an enumerator is reported as `undeclared identifier`. An enumerator with no field is reported as `type mismatch`. The field is an inline struct, `Circle struct { radius f32; };`, or the name of a struct, `Circle Circle;`. A field that is not a struct is reported as `type mismatch`. The value stores one payload and the enum tag after it. Every field starts at offset 0. The payload is large enough for the largest field, and the tag follows it. Assigning a field, or a field inside it, stores that enumerator. A designated initializer does the same. `Enum(value)` reads the tag. Reading a field does not check the tag.

`extern(abi="c") union` is a C union. Every field starts at offset 0, the size is the largest field, and the alignment is the strictest field.

`extern(abi="c") type Name enum` has no underlying type. `extern(abi="c") type Name enum(i8)` is reported as `extern enum`. An enumerator value is a decimal integer literal with no suffix, or with the suffix `i32`.

A trait method has no body and no receiver. A trait field is a name and a type, the same form as a struct field: `w i32;`. The struct that is passed must have a field of that name and that type.

## Types

A pointer is a leading star in every type position: `*i32`, `?*Point`, `*pkg.Point`, `*const u8`, `type Name *i32`, `sizeof(*i32)`, and `*i32(expr)`. `?` is written immediately before `*`. `*T` has a value. `?*T` may be `null`. `i32 *`, `Point *`, `sizeof(i32 *)`, and `i32*(expr)` are reported as `type mismatch`. An array is written in front of its element type in every position: `[4]i32`, `[N]i32`, `type Pair [2]i32`, and `func head(a [4]i32)`. An empty length is a slice: `[]i32` and `func head(a []i32) []i32`. `i32[4]`, `i32[N]`, and `i32[]` are reported as `type mismatch`.

```ebnf
go-type     = func-type
            | { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
            | "[" array-length "]" { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
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

One `*` before `[` is a pointer to an array, as in `*[4]i32`. `*const T` is a pointer to constant `T`. A store through it is reported as `type mismatch`. `*T` may be passed where `*const T` is required.

`int`, `uint`, `short`, `long`, `ulong`, `char`, `float`, and `double` are ordinary identifiers. Using one where a type is required is reported as `not supported yet`. `signed` and `unsigned` are reported as `type mismatch`.

An array uses `go-type` in every position, including a parameter and a result: `[4]i32` and `[N]i32`. `i32[4]` and `i32[]` are reported as `type mismatch`. A type alias uses the same order as `var`: `type Integer i32`, `type Ptr *Point`, `type Pair [2]i32`, `type BinOp func (i32, i32) i32`. The alias is a distinct type. A cast such as `Integer(n)` or `i32(value)` confirms the type. A file-scope constant can name a struct, a function pointer, or a typedef: `const value Integer = Integer(10)` and `const value = Integer(10)`. The function-pointer declarator `i32 (*name)(` parameters `)` still parses. A new variable is not written as a C declarator. `i32 value = 1` is reported as `expected var`.

## Statements

```ebnf
block       = "{" { statement } "}" ;
statement   = block
            | if-statement
            | "while" "(" expression ")" block
            | "do" statement "while" "(" expression ")" ";"
            | "for" "(" for-init [ expression ] ";" [ expression ] ")" block
            | "switch" "(" expression ")" statement
            | "case" expression ":" statement
            | "default" ":" statement
            | "return" [ expression { "," expression } ] ";"
            | "break" ";"
            | "continue" ";"
            | "defer" expression ";"
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

`if`, `while`, and `for` take a block. `else` takes a block or another `if`. A body without `{` is reported as `expected '{'`. `defer` takes one call expression. `assert` checks a constant integer at compile time. The string is the diagnostic when the condition is false. `case` and `default` are statements, so they appear inside the switch body.

## Expressions

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
unary       = new-expr
            | delete-expr
            | ( "+" | "-" | "!" | "~" | "&" | "*" | "sizeof" | "typeof" ) unary
            | postfix ;
postfix     = primary { postfix-op } ;
postfix-op  = "(" [ argument-list ] ")"
            | type-args "(" [ argument-list ] ")"
            | "." identifier
            | "[" expression "]"
            | "[" expression ":" expression "]"
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

Postfix `!` asserts that a `?*T` has a value. `pointer!` has type `*T`. `pointer!.value` reads the field. Prefix `!` remains the integer operator. There is no runtime check.

`a[i:j]` is the slice of elements from `i` up to, but not including, `j`. Both bounds are written. `a[i:]`, `a[:j]`, and `a[:]` are reported as `expected an expression`.

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

An anonymous function with no result type has result `void`. The short form is accepted where a function pointer is expected, and as a call argument whose type is a trait with one method. Its names and its result come from that type. A cast writes the type, then `(`, as in `i32(n)`, `*i32(p)`, `?*i32(p)`, or `func (i32) i32(p)`. A built-in name such as `i32` is always that cast. A name declared as a type in this file is a cast too: `Integer(n)`, `*Point(p)`, `ShapeType(shape)`. A cast to a struct is reported as `type mismatch`. `ShapeType(shape)` reads the tag of a tlc union, including from another file in the package. The cast has the type that was written, so `*i32(p)` is `*i32` and `?*i32(null)` is `?*i32`. `*i32(null)` is reported as `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type. `i32*(p)` is reported as `type mismatch`. `sizeof` of a parenthesized type is `sizeof(i32)`, `sizeof(*i32)`, `sizeof(enum Color)`, or `sizeof` of a declared name such as `sizeof(Point)`. `sizeof(struct Point)` is reported as `expected a type`. `sizeof(*p)` is the size of the dereferenced value when `p` is a variable. `sizeof` of a value is `sizeof` followed by a unary expression. `typeof` has the same shape and produces a `Type`. `typeof(i32)`, `typeof(*i32)`, and `typeof n` are accepted. `typeof(struct Point)` is reported as `expected a type`.

A struct initializer names fields: `{.value = 10, .complex = {.inner_value = 20}}`. The struct type comes from the destination. A field that is left out is zero. A trailing comma is allowed. `{10, 20}` is a value list. Where the destination is an array, it fills that array. Where a struct is expected, the value list is reported as `type mismatch`. An empty `{}` names no fields. Mixing a named field and a positional value is reported as `type mismatch`. `(40, 2)` is a tuple. `(40)` is the value `40`. `var value = (40, 2)` and `value := (40, 2)` bind that tuple. `var value1, value2 = value` and `a, b := value` unpack it. `value[0]` reads an element.

`struct-init` after a name, or after `name.name`, is a struct value: `Point{}`, `Point{.x = 1, .y = 2}`, and `geom.Point{}`. The type is that struct. Fields that are left out are zero. `var p = Point{}`, `const origin = Point{}`, and `p := Point{}` take the type from the name. `Point{1, 2}` is a value list and is reported as `type mismatch`. A name that is not a struct is reported as `type mismatch`. A `{` after a call, an index, or `value.field` is not this form. `new Point{.x = 1}` and `new(allocator) Point{.x = 1}` reserve a `Point` and write those fields. A field that is left out is zero. `new Point{1, 2}` and `new i32{1}` are reported as `type mismatch`. `new [n]T` reserves `n` elements and yields `*T`. `n` has type `i32`. `new [n]Point{{.x = 1}...}` stores that value in every element. `new [_]Point{{.x = 1}, {.y = 2}}` takes the length from the list, so the object is `[2]Point` and the result is `*[2]Point`. `new Box[i32]` allocates that specialization, and `new Box[i32]{.value = 1}` writes its fields. `new []T` is reported as `expected an expression`.

`[_]i32{10, 20, 12}` is an array literal. `_` stands for the length, which is the number of values, so the type is `[3]i32`. At file scope it initializes a variable when each value is a constant: `var row = [_]i32{10, 20, 12}`, `var grid = [_][2]i32{{1, 2}, {3, 4}}`, and `const fixed = [_]i32{19, 2}`.

## Tokens

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
assert bool break case const continue default defer delete do else enum
export extern false for func if import new null package
return sizeof struct switch test trait true type typeof union var void while
```

C keywords that tlc reserves but does not use are also keywords: `alignas`, `alignof`, `auto`, `constexpr`, `goto`, `inline`, `register`, `restrict`, `static_assert`, `thread_local`, `typeof_unqual`, `volatile`, and the spellings that start with `_` (`_Alignas`, `_Bool`, `_Complex`, and the rest of that set).

`as`, `from`, `impl`, `static`, and `this` are not keywords. `signed` and `unsigned` are not keywords.
