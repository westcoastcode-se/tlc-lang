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
            | fn-var
            | type-decl
            | assert-stmt
            | test-decl ;
```

`export`, `static`, and `extern` may stand, in any order, in front of a function, a method, a variable, or a `type`. `extern` may be followed by `(abi="c")` or `(abi="tlc")`. A bare `extern` means `abi="tlc"`. `extern(abi="c")` names a C library symbol and is linked only together with a C library (`-l` or a library file). `extern(abi="tlc")` names a tlc library symbol. `export static` is an error.

## Functions and methods

```ebnf
function    = "func" identifier [ type-params ] "(" [ parameters ] ")" result ( block | ";" ) ;
type-params = "[" type-param { "," type-param } "]"
            | "[" "..." identifier "]" ;
type-param  = identifier "type"
            | identifier integer-name
            | identifier ( "char" | "short" | "int" | "long" )
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

A function with no body is a prototype and ends with `;`. A body replaces that semicolon. `...` is allowed on an `extern(abi="c")` declaration and on a function with a body. Inside that body, `builtin.va()` reads the extra arguments. One parenthesized result is the same type as the bare result. `()` as a result list is an error.

### Methods

A receiver parameter is written `name this`, `name *this`, or `name ?*this`. `this` stands for the method's struct or enum, so a pointer is `*this` and an optional pointer is `?*this`. `this` is an ordinary identifier everywhere else. When the parameter list has no receiver, the function is static and the call is `Type.name(arguments)`.

The method is written in the same file as that struct or enum. A method in another file is reported as `method in another file`. Every file in the package can call it.

An inner function uses the same `function` form inside a block, and it has a body.

### Generics

`func name[T type, N int](value T) [N]T` is a generic function. `T type` is a type. `N int`, or another integer type, is a compile-time integer. `T Shape` requires a type that has `Shape`'s methods. `[...U]` is a type pack and is the last parameter in its brackets. A pack before another parameter is reported as `not supported yet`. One argument type makes `U` that type. Several make `U` a tuple. `func head[T type, ...U](value T, rest U) T` places the pack last. The call `id(1)` infers `T`. `repeat[4](7)` writes `N` and infers `T`. `id[int](1)` and `zero[int]()` write a type. `keep[int, char]((40, 'a'))` writes the tuple types. A method on a generic struct names that struct's parameters, `func Vec[T]::get(self *this) T`. After the call is specialized, `sizeof(T)`, `typeof(T)`, and `assert(N > 0, "empty")` use the concrete type and the integer. A bare result may be `typeof(expression)`, checked once the parameters exist.

## Variables

A typed variable names the type in Go order, after the name:

```ebnf
var-decl    = "var" identifier go-type [ "=" expression ] ";"
            | "var" identifier "=" expression ";"
            | identifier ":=" expression ";" ;
const-decl  = "const" identifier "=" expression ";" ;
```

`var name :=` is an error. Inside a function, several names unpack one tuple. A call that returns several results is that tuple:

```ebnf
binding     = "var" name-list "=" expression ";"
            | name-list ":=" expression ";"
            | name-list "=" expression ";" ;
```

`name :=` defines a name that is new in the block and assigns a name that already exists there. At least one name must be new. A `:=` whose names are all already in the block is `type mismatch`. `=` assigns names that already exist. `var` defines every name.

A variable whose type is a function pointer is written with `func` first, then the name:

```ebnf
fn-var      = func-type identifier { "," identifier } ";" ;
```

## Structs, enums, traits, and type aliases

```ebnf
type-decl   = "type" identifier [ type-params ] type-body ;
type-body   = "struct" [ align ] "{" { field } "}" [ ";" ]
            | "enum" [ "(" underlying ")" ] "{" [ enumerators ] "}" [ ";" ]
            | "trait" "{" { trait-method } "}" [ ";" ]
            | "union" "(" identifier ")" "{" { union-variant } "}" [ ";" ]
            | "union" "{" field { field } "}" [ ";" ]
            | go-type ";" ;
union-variant = identifier "struct" "{" { field } "}" ";"
              | field ;
align       = "(" "align" "=" integer-literal ")" ;
field       = identifier go-type ";" ;
declarator  = identifier [ array ]
            | "(" "*" { "*" } identifier ")" "(" [ func-args ] ")" ;

underlying  = integer-name | "bool" | "char" | "short" | "int" | "long" ;
enumerators = enumerator { "," enumerator } [ "," ] ;
enumerator  = identifier [ "=" integer-literal ] ;

trait-method = "func" identifier "(" [ parameters ] ")" result ";" ;
```

`type Vec[T type] struct` is a generic struct. The parameter list is allowed only before `struct`. A use is `Vec[int]` or `Vec[Point]`. A method on `Vec` is `func Vec[T]::get(self *this, i int) T`. The names are the struct's parameters, in that order. Leaving them out, or writing a different name, is reported as `type mismatch`. The wrong count is reported as `argument count`. `func Vec[T type]::get` is reported as `type mismatch`. `Vec` without arguments is reported as `type mismatch`. `Vec[int]` and `Vec[char]` are different types. Assigning one to the other, or passing one where the parameter is the other, is reported as `type mismatch`.

The semicolon after `}` is optional. A field is a name and then a `go-type`, the same order as `var`: `x int`, `next *Node`, `raw [8]char`, `op func (int, int) int`. A field still ends with `;`. `type Name struct(align=16)` sets the alignment. The literal is a positive power of two, at most 4096. A weaker alignment than the fields require is reported as `type mismatch`. `type Name struct` without `{` is reported as `expected '{'`. `type Name struct {}` is an empty struct. `sizeof` is 0, and the struct can have methods. `extern type Name struct {}` has that same size. An empty `extern(abi="c") struct` is written `extern(abi="c") type Name struct { }` and occupies one byte.

### Unions, enums, and traits

`type Name union(Enum)` is a tlc union. `Enum` is an enum in the same package. Each enumerator is one field, and each field names one enumerator. A field that is not an enumerator is reported as `undeclared identifier`. An enumerator with no field is reported as `type mismatch`. The field is an inline struct, `Circle struct { radius f32; };`, or the name of a struct, `Circle Circle;`. A field that is not a struct is reported as `type mismatch`. The value stores one payload and the enum tag after it. Every field starts at offset 0. The payload is large enough for the largest field, and the tag follows it. Assigning a field, or a field inside it, stores that enumerator. A designated initializer does the same. `Enum(value)` reads the tag. Reading a field does not check the tag.

`extern(abi="c") union` is a C union. Every field starts at offset 0, the size is the largest field, and the alignment is the strictest field.

`extern(abi="c") type Name enum` has no underlying type. `extern(abi="c") type Name enum(i8)` is reported as `extern enum`. An enumerator value is a decimal integer literal with no suffix, or with the suffix `i32`.

A trait method has no body and no receiver.

## Types

A pointer is a leading star in every type position: `*int`, `?*Point`, `*pkg.Point`, `*const char`, `type Name *int`, `sizeof(*int)`, and `*int(expr)`. `?` is written immediately before `*`. `*T` has a value. `?*T` may be `null`. `int *`, `Point *`, `sizeof(int *)`, and `int*(expr)` are reported as `type mismatch`. A `var`, a field, or a type alias writes an array in front of its element type: `[4]int`, `type Pair [2]int`. A parameter keeps the array after the element type: `int[4]`.

```ebnf
go-type     = func-type
            | { [ "?" ] "*" [ "const" ] } base-type [ type-args ]
            | "*" [ "const" ] "[" array-length "]" { [ "?" ] "*" [ "const" ] } base-type [ type-args ] ;

placed-type = func-type
            | { [ "?" ] "*" [ "const" ] } base-type [ array | type-args ]
            | go-type ;

array       = "[" [ array-length ] "]" ;
array-length = integer-literal | identifier ;
type-args   = "[" type-arg { "," type-arg } "]" ;
type-arg    = integer-literal
            | { [ "?" ] "*" } base-type [ type-args ]
            | array-length ;

func-type   = "func" "(" [ func-args ] ")" result ;
func-args   = placed-type { "," placed-type } ;

base-type   = integer-type
            | "void" | "bool" | "float" | "double"
            | integer-name | float-name
            | identifier
            | identifier "." identifier ;

integer-type = "char"
             | "short" [ "int" ]
             | "int"
             | "long" [ "int" ] ;

integer-name = "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "i64" | "u64"
             | "uint" | "ulong" ;
float-name   = "f32" | "f64" ;
```

One `*` before `[` is a pointer to an array, as in `*[4]i32`. An unsized `[]` is a parameter array, `int[]`. `*const T` is a pointer to constant `T`. A store through it is reported as `type mismatch`. `*T` may be passed where `*const T` is required.

`signed`, `unsigned`, and `long long` are not type names.

A parameter array stays after the element type. A type alias uses `go-type`, the same order as `var`: `type Integer int`, `type Ptr *Point`, `type Pair [2]int`, `type BinOp func (int, int) int`. The function-pointer declarator `int (*name)(` parameters `)` still parses. A new variable is not written as a C declarator. `int value = 1` is reported as `expected var`.

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
unary       = ( "+" | "-" | "!" | "~" | "&" | "*" | "sizeof" | "typeof" ) unary
            | postfix ;
postfix     = primary { postfix-op } ;
postfix-op  = "(" [ argument-list ] ")"
            | type-args "(" [ argument-list ] ")"
            | "." identifier
            | "[" expression "]"
            | "!" ;
argument-list = expression { "," expression } ;
```

`name[args](` is a generic call, not an index. `ops[i]()` is that call. `(ops[i])()` is the call of an element. An array length may be a file-scope `const` name: `[N]int`.

Postfix `!` asserts that a `?*T` has a value. `pointer!` has type `*T`. `pointer!.value` reads the field. Prefix `!` remains the integer operator. There is no runtime check.

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
            | struct-init ;

struct-init = "{" [ designator { "," designator } [ "," ] ] "}"
            | "{" expression { "," expression } [ "," ] "}" ;
designator  = "." identifier "=" expression ;

anonymous   = "func" "(" [ parameters ] ")" [ result ] block ;
short-function = "(" [ name-list ] ")" block ;
cast        = { "*" } scalar-type "(" expression ")"
            | { "*" } identifier "(" expression ")" ;
scalar-type = integer-type | "void" | "bool" | "float" | "double" | integer-name | float-name ;
```

An anonymous function with no result type has result `void`. The short form is accepted only where a function pointer is expected. Its names and its result come from that type. A cast writes the type, then `(`, as in `i32(n)`, `*int(p)`, or `?*int(p)`. A built-in name such as `i32` is always that cast. A name declared as a type in this file is a cast too: `Integer(n)`, `*Point(p)`, `ShapeType(shape)`. A cast to a struct is reported as `type mismatch`. `ShapeType(shape)` reads the tag of a tlc union, including from another file in the package. The cast has the type that was written, so `*int(p)` is `*int` and `?*int(null)` is `?*int`. `*int(null)` is reported as `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type. `int*(p)` is reported as `type mismatch`. `sizeof` of a parenthesized type is `sizeof(int)`, `sizeof(*int)`, `sizeof(enum Color)`, or `sizeof` of a declared name such as `sizeof(Point)`. `sizeof(struct Point)` is reported as `expected a type`. `sizeof(*p)` is the size of the dereferenced value when `p` is a variable. `sizeof` of a value is `sizeof` followed by a unary expression. `typeof` has the same shape and produces a `Type`. `typeof(int)`, `typeof(*int)`, and `typeof n` are accepted. `typeof(struct Point)` is reported as `expected a type`.

A struct initializer names fields: `{.value = 10, .complex = {.inner_value = 20}}`. The struct type comes from the destination. A field that is left out is zero. A trailing comma is allowed. `{10, 20}` is a value list. Where the destination is an array, it fills that array. Where a struct is expected, the value list is reported as `type mismatch`. An empty `{}` names no fields. Mixing a named field and a positional value is reported as `type mismatch`. `(40, 2)` is a tuple. `(40)` is the value `40`. `var value = (40, 2)` and `value := (40, 2)` bind that tuple. `var value1, value2 = value` and `a, b := value` unpack it. `value[0]` reads an element.

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
assert bool break case char const continue default defer do double else enum
export extern false float for func if import int long null package
return short sizeof static struct switch test trait true type typeof union var void while
```

C keywords that tlc reserves but does not use are also keywords: `alignas`, `alignof`, `auto`, `constexpr`, `goto`, `inline`, `register`, `restrict`, `static_assert`, `thread_local`, `typeof_unqual`, `volatile`, and the spellings that start with `_` (`_Alignas`, `_Bool`, `_Complex`, and the rest of that set).

`as`, `from`, `impl`, and `this` are not keywords. `signed` and `unsigned` are not keywords.
