# Structs and arrays

[Index](../README.md) · [Previous: typeof as a result](typeof.md) · [Next: Tests](test.md) · [Cost](../cost.md) · [Grammar](../grammar.md)

Structs are passed and returned by value. An array parameter or result names its length, as in `a [4]i32` and `[N]T`. The parameter becomes a pointer. A function may return an array. The return expression is a local array name, as in `return out` for `[N]T`. The call decays to a pointer, so `repeat[4](2)[0]` reads the first element. A slice parameter or result is `[]T`. It is passed and returned as the slice. It does not become a pointer. `row[i:j]` on an array produces `[]T`. An array parameter is already a pointer. `return row` from `func open(row [4]i32) []i32` is reported as `return type: expected []i32, found *i32`. `row[0:2]` in that function is `[]i32`. `row[i:]`, `row[:j]`, `row[:]`, and `row.len` use the length from `[4]`.

```c
type Point struct {
    x i32;
    y i32;
};

func sum(p Point) i32 {
    return p.x + p.y;
}

func pair() [2]i32 {
    var out [2]i32;
    out[0] = 40;
    out[1] = 2;
    return out;
}

func head(a [2]i32) i32 {
    return a[0];
}

func main() i32 {
    var p Point;
    p.x = 20;
    p.y = 22;
    return sum(p) + head(pair());
}
```

## Cost

Passing a struct of two `i32` fields copies them into the argument registers: 2 `movl`, then the traced call. The callee stores both and can add them in 5 instructions, the same as two `i32` parameters.

An array parameter is 1 `leaq` of the first element at the call. The callee's `return a[0]` is 4 instructions. `return a[0:2]` is 4: the pointer store, 1 reload, 1 `movl` of the length 2, and `retq`. `return a.len` is 3: the store, 1 `movl` of the length, and `retq`. Returning a local `[2]i32` is 5 instructions in the callee: the elements are stored and returned in `%eax` and `%edx`. The local is not zeroed first. The caller receives a pointer to that temporary and pays the traced call.

A slice parameter is the pointer and the length, passed in two registers. It is not decayed to a bare pointer.

[Index](../README.md) · [Previous: typeof as a result](typeof.md) · [Next: Tests](test.md) · [Cost](../cost.md) · [Grammar](../grammar.md)
