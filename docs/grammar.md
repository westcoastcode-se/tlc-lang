# Grammar

[Index](README.md)

This is the syntax of tlc. Meaning, types, and diagnostics are in the [language pages](README.md). A form that this grammar does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

## Notation

The grammar is EBNF. A name is a rule. `"text"` is a terminal. `[ x ]` is optional, `{ x }` is zero or more, and `( x )` groups. `|` separates alternatives. Rules are ordered so that an earlier alternative is the one the parser tries first when two could match.

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

A declaration is one of:

```ebnf
declaration = function
            | method
            | var-decl
            | fn-var
            | type-decl
            | assert-stmt
            | test-decl ;
```

`export`, `static`, and `extern` may stand, in any order, in front of a function, method, variable, or `type`. `extern` may be followed by `(abi="c")` or `(abi="tlc")`. A bare `extern` is `abi="tlc"`. `extern(abi="c")` names a C library symbol and is linked only together with a C library (`-l` or a library file). `extern(abi="tlc")` names a tlc library symbol. Another abi is `not supported yet`. `export static` is an error. `export` and `extern` on an inferred variable are rejected.

## Functions and methods

```ebnf
function    = "func" identifier "(" [ parameters ] ")" result ( block | ";" ) ;
method      = "func" identifier "::" identifier "(" receiver [ "," parameters ] ")" result ( block | ";" ) ;
receiver    = identifier [ "*" ] "this" ;

parameters  = parameter { "," parameter } [ "," "..." ] ;
parameter   = identifier placed-type ;

result      = placed-type
            | "(" placed-type { "," placed-type } ")" ;
```

A function with no body is a prototype and ends with `;`. A body replaces that semicolon. `...` is allowed only on an `extern(abi="c")` declaration. One parenthesized result is the same type as the bare result. `()` as a result list is an error.

A method's first parameter is the receiver, written `name this`, `name *this`, or `name ?*this`. `this` stands for the method's struct or enum, so a pointer is `*this` and an optional pointer is `?*this`. `self **this` is `not supported yet`. `this` is an ordinary identifier everywhere else.

The method is written in the same file as that struct or enum. A method in another file is `method in another file`. Every file in the package can call it.

An inner function uses the same `function` form inside a block. It has a body, and it is not `export`, `static`, `extern`, or a method.

## Variables

A typed variable names the type in Go order, after the name:

```ebnf
var-decl    = "var" identifier go-type [ "=" expression ] ";"
            | "var" identifier "=" expression ";"
            | identifier ":=" expression ";" ;
```

`var name :=` is an error. At file scope, `var a, b =` is rejected. Inside a function, several names can be bound from one call:

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
type-decl   = "type" identifier type-body ;
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

The semicolon after `}` is optional. A field is a name and then a `go-type`, the same order as `var`: `x int`, `next *Node`, `raw [8]char`, `op func (int, int) int`. A field still ends with `;`. `type Name struct(align=16)` sets the alignment. The literal is a positive power of two, at most 4096. A weaker alignment than the fields require is `type mismatch`. A pointer to an array (`*[4]int`) is `not supported yet` as a field. `type Name struct` without `{` is `expected '{'`. An empty `extern(abi="c") struct` is `extern(abi="c") type Name struct "{" "}"`.

`type Name union(Enum)` is a tlc union. `Enum` is an enum in the same package. A qualified name there is `not supported yet`. Each enumerator is one field, and each field names one enumerator. A field that is not an enumerator is `undeclared identifier`. An enumerator with no field is `type mismatch`. The field is an inline struct, `Circle struct { radius f32; };`, or the name of a struct, `Circle Circle;`. A field that is not a struct is `type mismatch`. The value stores the enum tag and one payload. Every field starts at the same offset after the tag. The payload is large enough for the largest field. Assigning a field, or a field inside it, stores that enumerator. A designated initializer does the same. `Enum(value)` reads the tag. Reading a field does not check the tag. `extern` and `extern(abi="c")` cannot use `union(Enum)`. That is `not supported yet`.

`extern(abi="c") union` is a C union: every field starts at offset 0, the size is the largest field, and the alignment is the strictest field. A union without `extern(abi="c")` and without an enum, including `extern union`, is `not supported yet`. An empty union is `not supported yet`.

`extern(abi="c") type Name enum` has no underlying type. `extern(abi="c") type Name enum(i8)` is `extern enum`. An enumerator value is a decimal integer literal with no suffix, or with the suffix `i32`.

A trait method has no body and no receiver.

## Types

A pointer is a leading star in every type position: `*int`, `?*Point`, `*pkg.Point`, `*const char`, `type Name *int`, `sizeof(*int)`, and `*int(expr)`. `?` is written immediately before `*`. `*T` has a value. `?*T` may be `null`. `?int` is `not supported yet`. `int *`, `Point *`, `sizeof(int *)`, and `int*(expr)` are `type mismatch`. A `var`, a field, or a type alias writes an array in front of its element type: `[4]int`, `type Pair [2]int`. A parameter keeps the array after the element type: `int[4]`.

```ebnf
go-type     = func-type
            | { [ "?" ] "*" } base-type
            | { [ "?" ] "*" } "[" integer-literal "]" { [ "?" ] "*" } base-type ;

placed-type = func-type
            | { [ "?" ] "*" } [ "const" ] base-type [ array ] ;

array       = "[" [ integer-literal ] "]" ;

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

One `*` before `[` is a pointer to an array, as in `*[4]i32`. `?*` before `[` is `not supported yet`. Two stars there, and a nested array, are rejected. An unsized `[]` is allowed on a parameter and rejected on a `var`. `const` is allowed on an `extern(abi="c")` parameter.

`signed`, `unsigned`, and `long long` are not type names.

A parameter array stays after the element type. A type alias uses `go-type`, the same order as `var`: `type Integer int`, `type Ptr *Point`, `type Pair [2]int`, `type BinOp func (int, int) int`. The function-pointer declarator `int (*name)(` parameters `)` still parses. A new variable is not written as a C declarator. `int value = 1` is `expected var`.

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

`if`, `while`, and `for` take a block. `else` takes a block or another `if`. A body without `{` is `expected '{'`. `defer` takes one call expression. `assert` checks a constant integer at compile time. The string is the diagnostic when the condition is false. A condition that is not a constant integer is `not supported yet`. `case` and `default` are statements, so they appear inside the switch body.

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
unary       = ( "+" | "-" | "!" | "~" | "&" | "*" | "sizeof" ) unary
            | postfix ;
postfix     = primary { postfix-op } ;
postfix-op  = "(" [ argument-list ] ")"
            | "." identifier
            | "[" expression "]"
            | "!" ;
argument-list = expression { "," expression } ;
```

Postfix `!` asserts that a `?*T` has a value. `pointer!` has type `*T`. `pointer!.value` reads the field. Prefix `!` is still the integer operator. There is no runtime check.

`?:`, `++`, `--`, `->`, and compound assignments such as `+=` are not part of the language.

```ebnf
primary     = identifier
            | integer-literal
            | float-literal
            | string
            | char-literal
            | "true" | "false" | "null"
            | "(" expression ")"
            | anonymous
            | short-function
            | cast
            | struct-init ;

struct-init = "{" [ designator { "," designator } [ "," ] ] "}" ;
designator  = "." identifier "=" expression ;

anonymous   = "func" "(" [ parameters ] ")" [ result ] block ;
short-function = "(" [ name-list ] ")" block ;
cast        = { "*" } scalar-type "(" expression ")"
            | { "*" } identifier "(" expression ")" ;
scalar-type = integer-type | "void" | "bool" | "float" | "double" | integer-name | float-name ;
```

An anonymous function with no result type has result `void`. The short form is only where a function pointer is expected; its names and its result come from that type. A cast writes the type, then `(`, as in `i32(n)`, `*int(p)`, or `?*int(p)`. A builtin name such as `i32` is always that cast. A name declared as a type in this file is a cast too: `Integer(n)`, `*Point(p)`, `ShapeType(shape)`. A cast to a struct is `type mismatch`. `ShapeType(shape)` reads the tag of a tlc union, including from another file in the package. The cast has the type that was written, so `*int(p)` is `*int` and `?*int(null)` is `?*int`. `*int(null)` is `type mismatch`. `f()` is a call when `f` is not a type. `*f()` dereferences the call when `f` is not a type. `int*(p)` is `type mismatch`. `sizeof` of a parenthesized type is `sizeof(int)`, `sizeof(*int)`, `sizeof(enum Color)`, or `sizeof` of a declared name such as `sizeof(Point)`. `sizeof(struct Point)` is `expected a type`. `sizeof(*p)` is the size of the dereferenced value when `p` is a variable. `sizeof` of a value is `sizeof` followed by a unary expression.

A struct initializer names fields: `{.value = 10, .complex = {.inner_value = 20}}`. The struct type comes from the destination. A field that is left out is zero. A positional element, such as `{10}`, is `not supported yet`. A trailing comma is allowed.

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
return short sizeof static struct switch test trait true type union var void while
```

C keywords that tlc reserves but does not use are also keywords: `alignas`, `alignof`, `auto`, `constexpr`, `goto`, `inline`, `register`, `restrict`, `static_assert`, `thread_local`, `typeof`, `typeof_unqual`, `volatile`, and the spellings that start with `_` (`_Alignas`, `_Bool`, `_Complex`, and the rest of that set).

`as`, `from`, `impl`, and `this` are not keywords. `signed` and `unsigned` are not keywords.
