# The tlc language

tlc is a compiled language in the C family. A program is a single entry file. A function is written `func name(parameter type) result`. A type is written after the name it describes, as in `var n i32` and `x i32` inside a struct. An import names a directory of source files. Structs and enums may have methods, and a struct may satisfy a trait by defining those methods.

The language accepts a subset of ISO C, together with the forms described in this reference. A syntactic form that the [grammar](grammar.md) does not list is not part of the language, even when the parser recognizes a similar C spelling and then rejects it.

A diagnostic has this form:

```text
file:line:column: error: message
```

Each feature page ends with the assembly of its smallest example, commented so the listing shows what runs. [Assembly](cost.md) is how those listings are taken, including the trace frame a call pushes. With no `-O` flag, `tlc` removes a store that is overwritten or never read, and does not run the other LLVM optimizations. `-O0` keeps that store. `-O1` and above run LLVM's pipeline. A listing is for a compile with no `-O` flag, on `x86_64-pc-linux-gnu`.

## Reference

### Compiler

- [Options](compiler/option.md)
- [Debug information](compiler/debug.md)
- [Language server](compiler/lsp.md)
- [Linking](compiler/link.md)
- [Symbol names](compiler/symbol.md)
- [A program](compiler/program.md)
- [Comments](compiler/comment.md)

### Packages

- [Packages](packages/package.md)
- [Imports](packages/import.md)
- [Libraries](packages/library.md)
- [Visibility](packages/visibility.md)
- [`extern`](packages/extern.md)

### Types

- [Numeric types](types/integer.md)
- [Pointers](types/pointer.md)
- [`void`, `bool`, and floating-point types](types/float.md)
- [Literals](types/literal.md)
- [Strings](types/string.md)
- [`sizeof`](types/sizeof.md)
- [Slices](types/slice.md)
- [The Type type](types/type.md)
- [Structs](types/struct.md)
- [Unions](types/union.md)
- [Arrays](types/array.md)
- [Enums](types/enum.md)
- [Type aliases](types/alias.md)
- [Keywords](keyword.md)

### Expressions

- [Expressions](expressions/operator.md)
- [Logical operators](expressions/logical.md)
- [Pointers and null](expressions/null.md)
- [Narrowing](expressions/narrowing.md)
- [Conditions](expressions/condition.md)
- [Type values](expressions/type.md)
- [Casts](expressions/cast.md)
- [Unsafe](expressions/unsafe.md)
- [Tuples and lists](expressions/tuple.md)

### Statements

- [Statements](statements/block.md)
- [`for`](statements/for.md)
- [`switch`](statements/switch.md)
- [`defer`](statements/defer.md)
- [Bindings](statements/binding.md)
- [`new` and `delete`](statements/new.md)
- [`assert`](statements/assert.md)

### Functions

- [Functions](functions/function.md)
- [Several results](functions/result.md)
- [Nested functions](functions/nested.md)
- [Generics](functions/generic.md)
- [Comptime](functions/comptime.md)
- [Macros](functions/macro.md)
- [`typeof` is not a result](functions/typeof.md)
- [Structs and arrays](functions/aggregate.md)
- [Tests](functions/test.md)
- [Function pointers](functions/pointer.md)

### Methods

- [Methods](methods/method.md)
- [Generic methods](methods/generic.md)
- [Static functions](methods/static.md)
- [Methods on primitive aliases](methods/alias.md)
- [Variables on a type](methods/variable.md)
- [Constants on a type](methods/constant.md)
- [Traits](methods/trait.md)

### Builtin

- [`trace`](builtin/trace.md)
- [`allocator`](builtin/allocator.md)
- [`va`](builtin/va.md)
- [`panic`](builtin/panic.md)
- [`memeql` and `memcmp`](builtin/mem.md)
- [`builtin/env/consts`](builtin/family.md)

### Standard library

- [`string`](std/string.md)
- [`seq`](std/seq.md)

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

The process exits with status 42. There is one `main`, and it belongs to the entry file. The sum is folded while compiling:

```asm
movl $42, %eax    # 40 + 2
retq
```
