# Documentation

`tlc` compiles a C dialect. The syntax is close to C. A function is written `func name(arg type) type`. You compile one entry file, import names a directory of source files, and types can have methods and traits. The dialect is a subset of ISO C with these additions.

Diagnostics look like `file:line:column: error: message`.

## Language

- [The compiler](compiler.md) — options, a program, and comments
- [Packages](packages.md) — imports, visibility, and `extern`
- [Types](types.md) — integers, floating types, and literals
- [Expressions](expressions.md) — operators, casts, and `null`
- [Statements](statements.md) — `if`, loops, `switch`, and `defer`
- [Functions](functions.md) — results, function pointers, and nested functions
- [Structs, pointers, and arrays](structs.md) — structs, unions, and layout
- [Methods and traits](methods.md)
- [Enums and type aliases](enums.md) — enums, `type` aliases, and keywords

## Grammar

The syntax is [one page](grammar.md). A form that page does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

- [Notation](grammar.md#notation)
- [Source file](grammar.md#source-file)
- [Functions and methods](grammar.md#functions-and-methods)
- [Variables](grammar.md#variables)
- [Structs, enums, traits, and type aliases](grammar.md#structs-enums-traits-and-type-aliases)
- [Types](grammar.md#types)
- [Statements](grammar.md#statements)
- [Expressions](grammar.md#expressions)
- [Tokens](grammar.md#tokens)
