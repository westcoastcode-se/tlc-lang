# The tlc language

tlc is a compiled language in the C family. A program is a single entry file. A function is written `func name(parameter type) result`. A type is written after the name it describes, as in `var n i32` and `x i32` inside a struct. An import names a directory of source files. Structs and enums may have methods, and a struct may satisfy a trait by defining those methods.

The language accepts a subset of ISO C, together with the forms described in this reference. A syntactic form that the [grammar](grammar.md) does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

A diagnostic has this form:

```text
file:line:column: error: message
```

## Reference

- [The compiler](compiler.md) — invoking `tlc`, debug information, and the language server
- [Packages](packages.md) — files, imports, visibility, and `extern`
- [Types](types.md) — integers, floating-point types, pointers, and literals
- [Expressions](expressions.md) — operators, casts, tuples, and `null`
- [Statements](statements.md) — control flow, bindings, and `defer`
- [Functions](functions.md) — results, generics, tests, and function pointers
- [Builtin](builtin.md) — `builtin.trace`, `builtin.va`, `builtin.panic`, and `consts.family`
- [Structs, pointers, and arrays](structs.md) — layout, initializers, and unions
- [Methods and traits](methods.md)
- [Enums and type aliases](enums.md)

## Grammar

The syntax is specified on [one page](grammar.md).

- [Notation](grammar.md#notation)
- [Source file](grammar.md#source-file)
- [Functions and methods](grammar.md#functions-and-methods)
- [Variables](grammar.md#variables)
- [Structs, enums, traits, and type aliases](grammar.md#structs-enums-traits-and-type-aliases)
- [Types](grammar.md#types)
- [Statements](grammar.md#statements)
- [Expressions](grammar.md#expressions)
- [Tokens](grammar.md#tokens)

## A first program

```c
func main() i32 {
    return 40 + 2;
}
```

```sh
tlc --run main.tlc
```

The process exits with status 42. There is one `main`, and it belongs to the entry file.
