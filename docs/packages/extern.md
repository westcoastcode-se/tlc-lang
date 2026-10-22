# extern

[Index](../README.md) · [Previous: Visibility](visibility.md) · [Next: Numeric types](../types/integer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

`extern` and `extern(abi="tlc")` name a symbol from a tlc library. The symbol is `qualifier.name`. The calling convention is the compiler's own, so the function may return several results. A method uses that same rule. The symbol is `qualifier.Type.name`, and the call is `value.name(...)` or `Type.name(...)` when the function has no receiver. A method on a generic struct is specialized with the struct, and each specialization is its own symbol. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that method are `extern(abi="c") on a trait or a generic`. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`. A trait default is copied onto each struct that gains it. `extern func Shape::twice` names that copy. The call stays `s.twice()` or `r.twice()`. `extern(abi="c")` and `extern(abi="c", name="symbol")` on that default are `extern(abi="c") on a trait or a generic`. The rules are on [Methods](../methods/method.md).

`extern(abi="c")` names a symbol from a C library. It keeps the C name and the C calling convention, and it returns one result. A method keeps the name `Type.name`. It is rejected on a trait or a generic, including a trait default, a generic function, a method template, a method of a generic struct, a function whose parameter is a trait, `extern(abi="c") type Name[T] struct`, `extern(abi="c") type Name trait`, and `extern(abi="c") type Name[T type] trait`. That error is `extern(abi="c") on a trait or a generic`. `extern` and `extern(abi="tlc")` stay allowed on a trait default, on a method of a generic struct, on a generic function, and on a method template. Each specialization is its own symbol. The call stays `id(value)` or `id[i32](value)`. On a method template the call stays `value.id[i32](value)`. A body defines that symbol. On a function whose parameter is a trait, `extern` and `extern(abi="tlc")` name that specialization. The call stays `use(r)` or `value.paint(s)`. Each struct is its own symbol, such as `qualifier.use.spec.Rect` or `qualifier.Rect.paint.spec.Circle`. A body defines that symbol. A prototype may be the declaration that says `extern`. A call that specializes a prototype with no body is `not supported yet`. `...` on that function is `not supported yet`. On a generic struct, and on a trait with no type parameter, `extern` and `extern(abi="tlc")` are `not supported yet`. `extern type Seq[T type] trait` and `extern(abi="tlc") type Seq[T type] trait` are accepted. That trait has no size and no symbol. `export` publishes it, and another package names `pkg.Seq[i32]`. The package `std/seq` declares that trait, and its `link` file names `libseq.a`. `export` publishes a function or an ordinary method whose parameter is a trait when the ABI is not C. `extern(abi="c") type Name union(Enum)`, a field `data union(Enum)`, and a field whose type is a tagged union, inside an `extern(abi="c")` struct or union, are `extern(abi="c") on a tagged union`. The C union is `extern(abi="c") type Name union { ... }`. `extern type Name union(Enum)` keeps the tag. Linking a program that uses `extern(abi="c")` requires a C library on the link line, either `-l` or a library file such as `libfoo.a`. `--check` and `--emit-*` do not link, so they accept the declaration. Without that library the link step reports `extern(abi="c") requires a C library`.

`extern(abi="c", name="symbol")` sets the C object symbol. The name in the program stays the identifier, so a call is written with that identifier. The arguments can be written in either order. `extern(name="real_add")` and `extern(abi="tlc", name="real_add")` are `name requires abi="c"`. A tlc library symbol is the compiler's name, `qualifier.name` or `qualifier.Type.name`. A variable uses the same rule: `extern(abi="c", name="errno")`. A file-scope constant uses that linkage too. `extern const n = 4` and `extern const Entity::next = 28` keep the folded value, and the symbol is `qualifier.n` or `qualifier.Entity.next`. `extern(abi="c", name="limit") const n = 4` is the C name `limit`. `name` adds no instructions. `extern(abi="c", name="symbol")` on a `type` is reported as `not supported yet`. An empty name is `type mismatch`. A repeated `abi` or `name` in one `extern` is `redefinition`. Two declarations of one function may omit `name` or repeat the same name. Two different names are `redefinition`.

A parameter or result of an `extern(abi="c")` function cannot be a function pointer that returns several results. A struct used by such a function, by value or through a pointer, must itself be `extern(abi="c")`. That includes a struct nested inside it. An `extern(abi="c") struct` uses the platform C memory layout, so its fields line up with a static or shared library. `bool` in that struct is one byte. On this host a struct of at most 16 bytes is passed in registers, and a larger struct is passed as a pointer. A function that is not `extern(abi="c")` keeps the compiler's own convention for struct values.

```c
type Point struct {
    x i32;
    y i32;
};

extern(abi="tlc") func parts(p Point) (i32, i32) {
    return p.x, p.y;
}

func main() i32 {
    var p Point;
    p.x = 20;
    p.y = 22;
    a, b := parts(p);
    return a + b;
}
```

```c
extern(abi="c") func printf(fmt *const u8, ...) i32;

func main() i32 {
    printf("hi\n");
    return 0;
}
```

```c
extern(abi="c", name="printf") func print(fmt *const u8, ...) i32;
extern(abi="c", name="errno") var last_error i32;

func main() i32 {
    print("hi\n");
    return last_error;
}
```

`print` is the name in the program. The object file links `printf` and `errno`.

`*const T` on a parameter, including an `extern(abi="c")` parameter, is a pointer to constant `T`. A variadic function may be declared with `extern(abi="c")` and without a body. A function with a body may use `...`, and `builtin.va()` reads the extra arguments.

## Cost

`extern(abi="c") func printf(fmt *const u8, ...) i32` followed by `printf("hi\n")` executes 36 instructions in `main` on the path where the trace holds fewer than 32 frames. The trace frame is the same one a tlc call pushes. The format string is one `leaq`, and the variadic call also executes `movb $0, %al` because no floating argument is passed. `printf` itself is outside this count. `extern(abi="c", name="printf") func print` followed by `print("hi\n")` executes the same 36 instructions. `name` changes the object symbol and adds none.

`extern(abi="tlc")` uses the same trace frame. A struct of at most 16 bytes is passed in registers under `extern(abi="c")`. Each `i32` field is one `movl` into an argument register after the value has been loaded. A larger struct is passed as a pointer. `extern(abi="c")` on a trait or a generic is rejected and adds no instructions. `extern func id[T type]` followed by `return id(40)` is the same 38 instructions in `main`, and the same 3 in the specialization, as that generic function without `extern`. `extern` changes the linkage and adds none. `extern func Point::id[T type]` followed by `return p.id[i32](40)` is that same 38 instructions in `main`, and the same 3 in the specialized method. The symbol is `qualifier.Point.id.spec.i32`. `extern` changes the linkage and adds none. `extern func use(s Shape)` followed by `return use(r)`, for a struct of two `i32` fields, is 8 instructions in `main` and does not push a trace frame. `extern(abi="tlc") func Rect::paint` followed by `return r.paint(c)`, storing 5 and 7, is 43 instructions in `main` when fewer than 32 frames are stored. That call pushes a trace frame. The symbol is `qualifier.Rect.paint.spec.Circle`. `extern` changes the linkage and adds none. `extern(abi="c")` on a tagged union is rejected and adds no instructions. A tagged union used as a field of an `extern(abi="c")` struct is rejected and adds no instructions.

[Index](../README.md) · [Previous: Visibility](visibility.md) · [Next: Numeric types](../types/integer.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
