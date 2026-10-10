# Grammar

This is the syntax of tlc. Meaning, types, and diagnostics are in [language.md](language.md). A form that this grammar does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

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
            | struct-decl
            | union-decl
            | enum-decl
            | typedef-decl
            | trait-decl ;
```

`export`, `static`, and `extern` may stand, in any order, in front of a function, method, variable, struct, enum, or typedef. `extern` may be followed by `(abi="c")` or `(abi="tlc")`. A bare `extern` is `abi="tlc"`. `extern(abi="c")` names a C library symbol and is linked only together with a C library (`-l` or a library file). `extern(abi="tlc")` names a tlc library symbol. Another abi is `not supported yet`. `export static` is an error. `export` and `extern` on an inferred variable are rejected.

## Functions and methods

```ebnf
function    = "func" identifier "(" [ parameters ] ")" result ( block | ";" ) ;
method      = "func" identifier "::" identifier "(" receiver [ "," parameters ] ")" result ( block | ";" ) ;
receiver    = "this" identifier placed-type ;

parameters  = parameter { "," parameter } [ "," "..." ] ;
parameter   = identifier placed-type ;

result      = placed-type
            | "(" placed-type { "," placed-type } ")" ;
```

A function with no body is a prototype and ends with `;`. A body replaces that semicolon. `...` is allowed only on an `extern(abi="c")` declaration. One parenthesized result is the same type as the bare result. `()` as a result list is an error.

A method's first parameter is the receiver. `this` is an ordinary identifier everywhere else.

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

`name :=` defines a name that is new in the block and assigns a name that already exists there. `=` assigns names that already exist. `var` defines every name.

A variable whose type is a function pointer is written with `func` first, then the name:

```ebnf
fn-var      = func-type identifier { "," identifier } ";" ;
```

## Structs, enums, traits, and typedefs

```ebnf
struct-decl = "struct" [ align ] identifier "{" { field } "}" [ ";" ] ;
align       = "(" "align" "=" integer-literal ")" ;
union-decl  = "extern" "(" "abi" "=" "\"c\"" ")" "union" identifier "{" field { field } "}" [ ";" ] ;
field       = identifier go-type ";" ;
declarator  = { "*" } identifier [ array ]
            | { "*" } "(" "*" { "*" } identifier ")" "(" [ func-args ] ")" ;

enum-decl   = "enum" identifier [ underlying ] "{" [ enumerators ] "}" [ ";" ] ;
underlying  = integer-name | "bool" | "char" | "short" | "int" | "long" ;
enumerators = enumerator { "," enumerator } [ "," ] ;
enumerator  = identifier [ "=" integer-literal ] ;

trait-decl  = "trait" identifier "{" { trait-method } "}" ";" ;
trait-method = "func" identifier "(" [ parameters ] ")" result ";" ;

typedef-decl = "typedef" placed-type declarator { "," declarator } ";"
            | "typedef" func-type identifier { "," identifier } ";" ;
```

The semicolon after `}` of a struct, a union, or an enum is optional. A field is a name and then a `go-type`, the same order as `var`: `x int`, `next *Node`, `raw [8]char`, `op func (int, int) int`. A field still ends with `;`. `struct(align=16)` sets the alignment. The literal is a positive power of two, at most 4096. A weaker alignment than the fields require is `type mismatch`. A pointer to an array (`*[4]int`) is `not supported yet` as a field. `struct Name;`, `union Name;`, and `enum Name;` are not declarations. An empty `extern(abi="c") struct` is `extern(abi="c") struct Name "{" "}"`.

`extern(abi="c") union` is a C union: every field starts at offset 0, the size is the largest field, and the alignment is the strictest field. A union without `extern(abi="c")`, including `extern union`, is `not supported yet`. An empty union is `not supported yet`.

`extern(abi="c") enum` has no underlying type. An enumerator value is a decimal integer literal with no suffix, or with the suffix `i32`.

A trait method has no body and no receiver.

## Types

Two orders are in use. A `var` type and a field type are Go order. A parameter, result, typedef, cast, and `sizeof` type is postfix.

```ebnf
go-type     = func-type
            | { "*" } base-type
            | { "*" } "[" integer-literal "]" { "*" } base-type ;

placed-type = func-type
            | base-type { "*" } [ array ]
            | "const" base-type { "*" } ;

array       = "[" [ integer-literal ] "]" ;

func-type   = "func" "(" [ func-args ] ")" result ;
func-args   = placed-type { "," placed-type } ;

base-type   = integer-type
            | "void" | "bool" | "float" | "double"
            | integer-name | float-name
            | identifier
            | identifier "." identifier
            | "struct" identifier
            | "enum" identifier ;

integer-type = "char"
             | "short" [ "int" ]
             | "int"
             | "long" [ "int" ] ;

integer-name = "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "i64" | "u64"
             | "uint" | "ulong" ;
float-name   = "f32" | "f64" ;
```

One `*` before `[` is a pointer to an array, as in `*[4]i32`. Two stars there, and a nested array, are rejected. An unsized `[]` is allowed on a parameter and rejected on a `var`. `const` is allowed on an `extern(abi="c")` parameter.

`signed`, `unsigned`, and `long long` are not type names.

A C declarator still parses in postfix position: stars before the name, `[` `]` after the name, and `(* name)(` parameters `)` for a function pointer. A new variable is not written that way. `int value = 1` is `expected var`.

## Statements

```ebnf
block       = "{" { statement } "}" ;
statement   = block
            | "if" "(" expression ")" statement [ "else" statement ]
            | "while" "(" expression ")" statement
            | "do" statement "while" "(" expression ")" ";"
            | "for" "(" for-init [ expression ] ";" [ expression ] ")" statement
            | "switch" "(" expression ")" statement
            | "case" expression ":" statement
            | "default" ":" statement
            | "return" [ expression { "," expression } ] ";"
            | "break" ";"
            | "continue" ";"
            | "defer" expression ";"
            | var-decl
            | binding
            | function
            | expression ";"
            | ";" ;

for-init    = ";" | var-decl | binding | expression ";" ;
```

`defer` takes one call expression. `case` and `default` are statements, so they appear inside the switch body.

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
            | "[" expression "]" ;
argument-list = expression { "," expression } ;
```

`?:`, `++`, `--`, `->`, and compound assignments such as `+=` are not part of the language.

```ebnf
primary     = identifier
            | integer-literal
            | float-literal
            | string
            | char-literal
            | "true" | "false" | "nullptr"
            | "(" expression ")"
            | anonymous
            | short-function
            | cast ;

anonymous   = "func" "(" [ parameters ] ")" [ result ] block ;
short-function = "(" [ name-list ] ")" block ;
cast        = scalar-type { "*" } "(" expression ")"
            | ( identifier | "struct" identifier | "enum" identifier ) "*" { "*" } "(" expression ")" ;
scalar-type = integer-type | "void" | "bool" | "float" | "double" | integer-name | float-name ;
```

An anonymous function with no result type has result `void`. The short form is only where a function pointer is expected; its names and its result come from that type. A cast is a scalar type, or a name with at least one `*`, followed by `(`, as in `i32(n)` or `int*(p)`. `Point(x)` is a call. `sizeof` of a parenthesized type is `sizeof "(" scalar-type { "*" } ")"`, or the same with `struct` or `enum` before the name. `sizeof` of a value is `sizeof` followed by a unary expression.

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
bool break case char const continue default defer do double else enum
export extern false float for func if import int long nullptr package
return short sizeof static struct switch trait true typedef var void while
```

C keywords that tlc reserves but does not use are also keywords: `alignas`, `alignof`, `auto`, `constexpr`, `goto`, `inline`, `register`, `restrict`, `static_assert`, `thread_local`, `typeof`, `typeof_unqual`, `union`, `volatile`, and the spellings that start with `_` (`_Alignas`, `_Bool`, `_Complex`, and the rest of that set).

`as`, `from`, `impl`, and `this` are not keywords. `signed` and `unsigned` are not keywords.
