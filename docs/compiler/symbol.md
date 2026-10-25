# Symbol names

[Index](../README.md) · [Previous: Linking](link.md) · [Next: A program](program.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)

A symbol is named `qualifier.name`. Each parameter list of one name is a separate object when there are several lists. A name with one list stays `qualifier.name`. `extern` and `extern(abi="tlc")` use that name and are linked from a tlc library. A generic function is specialized per call, so `extern` names that specialization, such as `qualifier.id.spec.i32`. A function with a comptime parameter beside a runtime parameter is specialized per constant, such as `qualifier.scale.spec.2`. An enumerator uses its representation, such as `qualifier.take.spec.1`. That parameter is not an argument of the copy. `extern` and `extern(abi="tlc")` also keep `qualifier.scale`. That symbol passes the comptime parameter. The compiler calls the copy when the argument is a constant, and `qualifier.scale` when the argument is a runtime value or the function is used as a value. A method template is specialized per call, so `extern` names that specialization, such as `qualifier.Point.id.spec.i32`. A function whose parameter is a trait is specialized per struct, so `extern` names that specialization, such as `qualifier.use.spec.Rect` or `qualifier.Rect.paint.spec.Circle`. Several parameter lists put the trait in that name, such as `qualifier.take.spec.Named.Rect`. An exported variable on a generic struct is one symbol per specialization, such as `qualifier.Box.spec.i32.extra`. A struct from another package keeps its package in that name, such as `geom.measure.spec.main.Circle`. `extern(abi="c")` keeps the C name. `extern(abi="c", name="symbol")` replaces that C object name. The identifier in the program stays the name used in calls and in other files. `extern(name="symbol")` and `extern(abi="tlc", name="symbol")` are `name requires abi="c"`. On a trait default, `extern` uses the compiler name of each copy. `extern(abi="c")` on a trait default or on a method of a generic struct is `extern(abi="c") on a trait or a generic`.

```c
func main() i32 {
    return 0;
}
```

```asm
main:
xorl %eax, %eax                  # zero the result
retq                             # return
```

[Index](../README.md) · [Previous: Linking](link.md) · [Next: A program](program.md) · [Assembly](../cost.md) · [Grammar](../grammar.md)
